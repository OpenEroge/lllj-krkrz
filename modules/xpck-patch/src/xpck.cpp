// XPCK translation patch packs (see include/compat/xpck_patch.h).
//
// XPCK format (tools/formats/xpck_unpack.py): "XPCK", u32 index offset at 10;
// index trailer u8 method + u64 packed/unpacked size, zstd; File{info,segm}
// chunks with leaf names only; data in 2 MiB segments, one zstd frame each.
#include "tjsCommHead.h"
#include "compat/compat.h"
#include "compat/xpck_patch.h"
#include "compat/subtitles.h"
#include "EngineHooks.h"
#include "toml.hpp"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsDictionary.h"
#include "CharacterSet.h"
#include <SDL3/SDL.h>
#include <zstd.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

struct XpckSegment {
	tjs_uint32 method, offset, size, packed;
	tjs_uint64 start; // position of the segment inside the file
};

struct XpckEntry {
	ttstr name; // original spelling
	tjs_uint64 size = 0;
	std::vector<XpckSegment> segs;
};

std::u16string Lower(const ttstr &s)
{
	std::u16string r(reinterpret_cast<const char16_t *>(s.c_str()), s.GetLen());
	for (auto &c : r) if (c >= u'A' && c <= u'Z') c = c - u'A' + u'a';
	return r;
}

class XpckArchive {
public:
	int Fd = -1;
	std::unordered_map<std::u16string, XpckEntry> Entries;

	~XpckArchive() { if (Fd >= 0) close(Fd); }

	bool ReadAt(tjs_uint64 off, void *buf, size_t n) const
	{
		auto *p = static_cast<char *>(buf);
		while (n) {
			ssize_t r = pread(Fd, p, n, (off_t)off);
			if (r <= 0) return false;
			p += r; off += r; n -= r;
		}
		return true;
	}

	bool Open(const std::string &path)
	{
		Fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (Fd < 0) return false;
		unsigned char head[14];
		if (!ReadAt(0, head, sizeof head) || memcmp(head, "XPCK", 4)) return false;
		tjs_uint32 idxOff;
		memcpy(&idxOff, head + 10, 4);
		unsigned char trailer[17];
		if (!ReadAt(idxOff, trailer, sizeof trailer)) return false;
		tjs_uint64 packed, size;
		memcpy(&packed, trailer + 1, 8);
		memcpy(&size, trailer + 9, 8);
		if (packed > (64u << 20) || size > (256u << 20)) return false;
		std::vector<unsigned char> raw(packed), idx(size);
		if (!ReadAt(idxOff + 17, raw.data(), packed)) return false;
		if (trailer[0] == 2) {
			size_t n = ZSTD_decompress(idx.data(), size, raw.data(), packed);
			if (ZSTD_isError(n) || n != size) return false;
		} else if (trailer[0] == 0 && packed == size) {
			idx = raw;
		} else {
			return false;
		}
		return ParseIndex(idx);
	}

private:
	template <class F> static bool Chunks(const unsigned char *p, size_t n, F f)
	{
		size_t pos = 0;
		while (pos + 12 <= n) {
			tjs_uint64 len;
			memcpy(&len, p + pos + 4, 8);
			if (len > n - pos - 12) return false;
			if (!f(p + pos, p + pos + 12, (size_t)len)) return false;
			pos += 12 + len;
		}
		return pos == n;
	}

	bool ParseIndex(const std::vector<unsigned char> &idx)
	{
		return Chunks(idx.data(), idx.size(), [&](const unsigned char *tag, const unsigned char *body, size_t len) {
			if (memcmp(tag, "File", 4)) return true;
			XpckEntry e;
			bool ok = Chunks(body, len, [&](const unsigned char *t, const unsigned char *b, size_t l) {
				if (!memcmp(t, "info", 4)) {
					if (l < 14) return false;
					tjs_uint32 usz; tjs_uint16 nl;
					memcpy(&usz, b + 4, 4);
					memcpy(&nl, b + 12, 2);
					if (14u + nl > l) return false;
					e.size = usz;
					tjs_string wide;
					if (!TVPUtf8ToUtf16(wide, std::string(reinterpret_cast<const char *>(b + 14), nl))) return false;
					e.name = ttstr(wide.c_str());
				} else if (!memcmp(t, "segm", 4)) {
					tjs_uint64 start = 0;
					for (size_t i = 0; i + 16 <= l; i += 16) {
						XpckSegment s;
						memcpy(&s.method, b + i, 4);
						memcpy(&s.offset, b + i + 4, 4);
						memcpy(&s.size, b + i + 8, 4);
						memcpy(&s.packed, b + i + 12, 4);
						s.start = start;
						start += s.size;
						e.segs.push_back(s);
					}
				}
				return true;
			});
			if (!ok || e.name.IsEmpty()) return false;
			tjs_uint64 total = 0;
			for (auto &s : e.segs) total += s.size;
			if (total != e.size) return false;
			Entries[Lower(e.name)] = std::move(e);
			return true;
		});
	}
};

class XpckStream final : public tTJSBinaryStream {
	std::shared_ptr<const XpckArchive> Arc;
	const XpckEntry &Entry;
	tjs_uint64 Pos = 0;
	size_t CachedSeg = (size_t)-1;
	std::vector<unsigned char> Buf, Packed;

	void Load(size_t i)
	{
		if (i == CachedSeg) return;
		const XpckSegment &s = Entry.segs[i];
		Buf.resize(s.size);
		if (s.method == 2) {
			Packed.resize(s.packed);
			if (!Arc->ReadAt(s.offset, Packed.data(), s.packed))
				throw std::runtime_error("xpck: read failed");
			size_t n = ZSTD_decompress(Buf.data(), s.size, Packed.data(), s.packed);
			if (ZSTD_isError(n) || n != s.size) throw std::runtime_error("xpck: bad zstd segment");
		} else if (s.method == 0 && s.packed == s.size) {
			if (!Arc->ReadAt(s.offset, Buf.data(), s.size))
				throw std::runtime_error("xpck: read failed");
		} else {
			throw std::runtime_error("xpck: unsupported segment method");
		}
		CachedSeg = i;
	}

public:
	XpckStream(std::shared_ptr<const XpckArchive> arc, const XpckEntry &e) : Arc(std::move(arc)), Entry(e) {}

	tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) override
	{
		tjs_int64 base = whence == TJS_BS_SEEK_CUR ? (tjs_int64)Pos
			: whence == TJS_BS_SEEK_END ? (tjs_int64)Entry.size : 0;
		tjs_int64 np = base + offset;
		if (np < 0) np = 0;
		if ((tjs_uint64)np > Entry.size) np = Entry.size;
		Pos = np;
		return Pos;
	}
	tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint count) override
	{
		auto *out = static_cast<unsigned char *>(buffer);
		tjs_uint done = 0;
		while (done < count && Pos < Entry.size) {
			// segments are 2 MiB; a binary search keeps random access cheap
			size_t lo = 0, hi = Entry.segs.size();
			if (CachedSeg != (size_t)-1 && Pos >= Entry.segs[CachedSeg].start &&
				Pos < Entry.segs[CachedSeg].start + Entry.segs[CachedSeg].size) {
				lo = CachedSeg;
			} else {
				while (hi - lo > 1) {
					size_t mid = (lo + hi) / 2;
					if (Entry.segs[mid].start <= Pos) lo = mid; else hi = mid;
				}
			}
			Load(lo);
			const XpckSegment &s = Entry.segs[lo];
			tjs_uint64 in = Pos - s.start;
			tjs_uint64 n = std::min<tjs_uint64>(s.size - in, count - done);
			memcpy(out + done, Buf.data() + in, (size_t)n);
			done += (tjs_uint)n;
			Pos += n;
		}
		return done;
	}
	tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) override { return 0; }
	void TJS_INTF_METHOD SetEndOfStorage() override {}
	tjs_uint64 TJS_INTF_METHOD GetSize() override { return Entry.size; }
};

std::shared_ptr<const XpckArchive> Pack;
std::once_flag PackOnce;
std::thread::id MainThread = std::this_thread::get_id(); // static init runs on the main thread

const XpckEntry *FindEntry(const ttstr &leaf)
{
	if (!Pack) return nullptr;
	auto it = Pack->Entries.find(Lower(leaf));
	return it == Pack->Entries.end() ? nullptr : &it->second;
}

class XpckMedia final : public iTVPStorageMedia2 {
	std::atomic<tjs_uint> RefCount{1};
	static ttstr Leaf(const ttstr &name)
	{
		// names arrive as "./leaf" (domain "." + path)
		const tjs_char *p = name.c_str();
		const tjs_char *slash = TJS_strrchr(p, TJS_W('/'));
		return ttstr(slash ? slash + 1 : p);
	}
public:
	void TJS_INTF_METHOD AddRef() override { ++RefCount; }
	void TJS_INTF_METHOD Release() override { if (--RefCount == 0) delete this; }
	void TJS_INTF_METHOD GetName(ttstr &name) override { name = TJS_W("xpck"); }
	void TJS_INTF_METHOD NormalizeDomainName(ttstr &) override {}
	void TJS_INTF_METHOD NormalizePathName(ttstr &) override {}
	bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) override { return FindEntry(Leaf(name)) != nullptr; }
	tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &name, tjs_uint32 flags) override
	{
		if ((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ) throw std::runtime_error("xpck media is read-only");
		const XpckEntry *e = FindEntry(Leaf(name));
		if (!e) throw std::runtime_error("xpck: file not found");
		return new XpckStream(Pack, *e);
	}
	void TJS_INTF_METHOD GetListAt(const ttstr &, iTVPStorageLister *lister) override
	{
		if (Pack) for (auto &kv : Pack->Entries) lister->Add(kv.second.name);
	}
	void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) override { name.Clear(); }
	bool TJS_INTF_METHOD Remove(const ttstr &) override { return false; }
	bool TJS_INTF_METHOD Move(const ttstr &, const ttstr &) override { return false; }
	tjs_uint64 TJS_INTF_METHOD LastModifiedFileTime(const ttstr &) override { return 0; }
	tjs_uint64 TJS_INTF_METHOD FileSize(const ttstr &name) override
	{
		const XpckEntry *e = FindEntry(Leaf(name));
		return e ? e->size : 0;
	}
};

ttstr Utf8(const std::string &s)
{
	tjs_string w;
	TVPUtf8ToUtf16(w, s);
	return ttstr(w.c_str());
}

// --- configuration (Install) ----------------------------------------------
struct Redirect { std::u16string map[3]; size_t language; bool keep[3]; };
struct Config {
	ttstr option;                     // -<option>=no|yes|<path>
	std::string pack;                 // file name next to the executable
	std::vector<ttstr> languages;     // TJS expressions -> 0 / 1 / 2
	std::unordered_map<std::u16string, Redirect> redirects;
	std::vector<std::pair<std::string, std::string>> subtitles; // movie leaf -> .ass
	std::vector<std::pair<std::string, std::string>> fonts;     // family -> file in the pack
} Cfg;

void OpenPack()
{
	ttstr v;
	std::string path;
	if (compat::Option(Cfg.option.c_str(), v)) {
		if (v == TJS_W("no")) return;
		if (v != TJS_W("yes")) TVPUtf16ToUtf8(path, v.AsStdString());
	}
	if (path.empty()) {
		const char *base = SDL_GetBasePath();
		path = std::string(base ? base : "./") + Cfg.pack;
	}
	auto arc = std::make_shared<XpckArchive>();
	if (!arc->Open(path)) {
		compat::Log(ttstr(TJS_W("xpck: no usable pack at ")) + Utf8(path) + TJS_W(", patch disabled"));
		return;
	}
	compat::Log(ttstr(TJS_W("xpck: mounted ")) + Utf8(path) + TJS_W(" (") +
		ttstr((tjs_int)arc->Entries.size()) + TJS_W(" files)"));
	Pack = std::move(arc);
	auto *media = new XpckMedia();
	TVPRegisterStorageMedia(media);
	media->Release();
}

// Set by Install(), once the command line is parsed.
// Earlier lookups are the engine's own startup files, which the pack does not have.
std::atomic<bool> Ready{false};

void EnsurePack() { std::call_once(PackOnce, OpenPack); }

// Language indexes as the DLL evaluates them. TJS may only run on the main
// thread; other threads (image prefetch, movie decoding) use the last value.
std::vector<std::unique_ptr<std::atomic<int>>> LastLang;

int LanguageIndex(size_t language)
{
	if (std::this_thread::get_id() == MainThread) {
		int idx = -1;
		try {
			tTJSVariant v;
			TVPExecuteExpression(Cfg.languages[language], &v);
			if (v.Type() == tvtInteger) idx = (int)(tjs_int)v;
		} catch (...) {
		}
		*LastLang[language] = idx;
		return idx;
	}
	return *LastLang[language];
}

bool IsBareName(const ttstr &name)
{
	for (const tjs_char *p = name.c_str(); *p; ++p)
		if (*p == TJS_W('/') || *p == TJS_W('\\') || *p == TJS_W(':') || *p == TJS_W('>')) return false;
	return !name.IsEmpty();
}

} // namespace

bool compat::xpck::Enabled()
{
	if (!Ready) return false;
	EnsurePack();
	return (bool)Pack;
}

ttstr compat::xpck::Redirect(const ttstr &leaf)
{
	if (!Enabled()) return leaf;
	auto it = Cfg.redirects.find(Lower(leaf));
	if (it == Cfg.redirects.end()) return leaf;
	int idx = LanguageIndex(it->second.language);
	if (idx < 0 || idx > 2 || it->second.keep[idx]) return leaf;
	return ttstr(reinterpret_cast<const tjs_char *>(it->second.map[idx].c_str()));
}

bool compat::xpck::ReadFile(const ttstr &leaf, std::vector<unsigned char> &data)
{
	if (!Enabled()) return false;
	const XpckEntry *e = FindEntry(leaf);
	if (!e) return false;
	XpckStream s(Pack, *e);
	data.resize((size_t)e->size);
	return s.Read(data.data(), (tjs_uint)data.size()) == data.size();
}

namespace {

// Bare names first go through the redirect table, then the pack. Runs before
// the engine's cache, because a redirect depends on the current language.
tTVPStorageRedirect RedirectHook(const ttstr &name, ttstr &out)
{
	if (!IsBareName(name) || !compat::xpck::Enabled()) return tTVPStorageRedirect::None;
	ttstr leaf = compat::xpck::Redirect(name);
	if (FindEntry(leaf)) {
		out = ttstr(TJS_W("xpck://./")) + leaf;
		return tTVPStorageRedirect::Placed;
	}
	if (leaf != name) {
		out = leaf;
		return tTVPStorageRedirect::Renamed;
	}
	return tTVPStorageRedirect::None;
}

// A path that does not exist falls back to its leaf; the pack comes first there too.
ttstr LeafHook(const ttstr &leaf)
{
	if (leaf.IsEmpty() || !compat::xpck::Enabled() || !FindEntry(leaf)) return ttstr();
	return ttstr(TJS_W("xpck://./")) + leaf;
}

std::string LowerLeaf(const std::string &path)
{
	size_t p = path.find_last_of("/\\>");
	std::string leaf = p == std::string::npos ? path : path.substr(p + 1);
	for (auto &c : leaf) c = (char)std::tolower((unsigned char)c);
	return leaf;
}

bool ProvideSubtitles(const std::string &movie, compat::SubtitleScript &script)
{
	if (!compat::xpck::Enabled()) return false;
	const std::string leaf = LowerLeaf(movie);
	for (auto &s : Cfg.subtitles) {
		if (s.first != leaf) continue;
		const ttstr name = compat::xpck::Redirect(Utf8(s.second));
		// e.g. Japanese: the table maps op.ass to itself, which the pack does not have
		if (!compat::xpck::ReadFile(name, script.data)) return false;
		TVPUtf16ToUtf8(script.name, name.AsStdString());
		return true;
	}
	return false;
}

void ProvideFonts(std::vector<compat::SubtitleFont> &fonts)
{
	for (auto &f : Cfg.fonts) {
		compat::SubtitleFont font;
		font.family = f.first;
		if (compat::xpck::ReadFile(Utf8(f.second), font.data)) fonts.push_back(std::move(font));
		else compat::Log(Utf8("xpck: missing subtitle font " + f.second));
	}
}

std::u16string U16(const std::string &utf8)
{
	tjs_string w;
	TVPUtf8ToUtf16(w, utf8);
	return std::u16string(reinterpret_cast<const char16_t *>(w.c_str()), w.size());
}

void ParseConfig(const char *text)
{
	toml::table t = toml::parse(text);
	Cfg.option = Utf8("-" + t["option"].value_or(std::string("xpck")));
	Cfg.pack = t["pack"].value_or(std::string());
	std::vector<std::string> names; // language name -> index into Cfg.languages
	if (auto *langs = t["languages"].as_table()) {
		for (auto &[key, value] : *langs) {
			names.emplace_back(key.str());
			Cfg.languages.push_back(Utf8(value.value_or(std::string("0"))));
			LastLang.emplace_back(new std::atomic<int>(-1));
		}
	}
	auto language = [&](const std::string &n) -> size_t {
		for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return i;
		throw std::runtime_error("xpck config: unknown language expression " + n);
	};
	if (auto *list = t["redirect"].as_array()) {
		for (auto &node : *list) {
			auto &r = *node.as_table();
			Redirect red{};
			red.language = language(r["language"].value_or(std::string()));
			auto *to = r["to"].as_array();
			if (!to || to->size() != 3) throw std::runtime_error("xpck config: redirect.to needs 3 names");
			for (size_t i = 0; i < 3; ++i) {
				std::string n = (*to)[i].value_or(std::string());
				red.keep[i] = n.empty(); // "" = unchanged
				red.map[i] = U16(n);
			}
			std::string from = r["from"].value_or(std::string());
			for (auto &c : from) c = (char)std::tolower((unsigned char)c);
			Cfg.redirects[U16(from)] = red;
		}
	}
	if (auto *list = t["subtitles"].as_array()) {
		for (auto &node : *list) {
			auto &e = *node.as_table();
			std::string movie = e["movie"].value_or(std::string());
			for (auto &c : movie) c = (char)std::tolower((unsigned char)c);
			Cfg.subtitles.emplace_back(movie, e["ass"].value_or(std::string()));
		}
	}
	if (auto *list = t["subtitle_fonts"].as_array()) {
		for (auto &node : *list) {
			auto &e = *node.as_table();
			Cfg.fonts.emplace_back(e["family"].value_or(std::string()), e["file"].value_or(std::string()));
		}
	}
}

} // namespace

void compat::xpck::Install(const char *tomlConfig)
{
	ParseConfig(tomlConfig);
	Ready = true;
	EnsurePack();
	TVPAddStorageRedirectHook(RedirectHook);
	TVPAddStorageLeafHook(LeafHook);
	compat::AddSubtitleProvider(ProvideSubtitles);
	compat::AddSubtitleFontProvider(ProvideFonts);
}

// CompatNative.xpckEnabled(): the pack is mounted
COMPAT_NATIVE(xpckEnabled)
{
	if (result) *result = (tjs_int)(compat::xpck::Enabled() ? 1 : 0);
	return TJS_S_OK;
}

// CompatNative.xpckRedirect(name): the name the redirect table resolves to now
COMPAT_NATIVE(xpckRedirect)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	if (result) *result = compat::xpck::Redirect(ttstr(*param[0]));
	return TJS_S_OK;
}
