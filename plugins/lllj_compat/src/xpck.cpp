// Chinese patch (lllj_zh.exe / lllj_zh.dll) replacement, storage side.
//
// lllj_zh.dll is a Detours hook DLL. Its whole behaviour is driven by a JSON
// config embedded in its resources (RVA 0x29b0f0); the parts reproduced here:
//   * PatchPacks ["lllj_zh.pck"]: files in the pack win over the game's xp3 data.
//   * Multilang.RedirectTable: on every open, a listed name is replaced by
//     Map[language] (null = unchanged). "Eval":0 takes the language from
//     (typeof kag == "Object") ? kag.languageType : 0, "Eval":1 from
//     SystemConfig.uiLanguageType. Indexes are 0 jp, 1 cn, 2 tw.
//   * Subtitles / SubtitleFonts, consumed by FFmpegMoviePlayer.cpp.
// SaveDataPath is handled by run.sh (-datapath); the remaining DLL hooks are
// Windows specific (see PLAN.md, batch 6).
//
// XPCK format: tools/xpck_unpack.py. The pack is opened from -lllj_zh=<path>,
// else <exe dir>/lllj_zh.pck; -lllj_zh=no disables the patch.

#include "tjsCommHead.h"
#include "lllj_compat.h"
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

void OpenPack()
{
	tTJSVariant opt;
	std::string path;
	if (TVPGetCommandLine(TJS_W("-lllj_zh"), &opt)) {
		ttstr v(opt);
		if (v == TJS_W("no")) return;
		if (v != TJS_W("yes")) TVPUtf16ToUtf8(path, v.AsStdString());
	}
	if (path.empty()) {
		const char *base = SDL_GetBasePath();
		path = std::string(base ? base : "./") + "lllj_zh.pck";
	}
	auto arc = std::make_shared<XpckArchive>();
	if (!arc->Open(path)) {
		TVPAddImportantLog(ttstr(TJS_W("(lllj) zh: no usable pack at ")) + Utf8(path) +
			TJS_W(", running the Japanese game"));
		return;
	}
	TVPAddImportantLog(ttstr(TJS_W("(lllj) zh: mounted ")) + Utf8(path) + TJS_W(" (") +
		ttstr((tjs_int)arc->Entries.size()) + TJS_W(" files)"));
	Pack = std::move(arc);
	auto *media = new XpckMedia();
	TVPRegisterStorageMedia(media);
	media->Release();
}

// Set once the command line is parsed (LLLJZhInit from the LLLJNative install).
// Earlier lookups are the engine's own startup files, which the pack does not have.
std::atomic<bool> Ready{false};

void EnsurePack() { std::call_once(PackOnce, OpenPack); }

// --- config copied from lllj_zh.dll's embedded JSON -----------------------
struct Redirect { const char16_t *map[3]; int eval; };

const std::unordered_map<std::u16string, Redirect> &RedirectTable()
{
	static const std::unordered_map<std::u16string, Redirect> t = [] {
		std::unordered_map<std::u16string, Redirect> m;
		static const char16_t *titles[] = {u"title_bg", u"title_bg1", u"title_bg2", u"title_bg3", u"title_bg4",
			u"title_bg5", u"title_bg6", u"title_bg7", u"title_bg_trial"};
		static std::vector<std::u16string> keep; // owns the mapped names
		keep.reserve(64);
		for (auto b : titles) {
			keep.push_back(std::u16string(b) + u"_cn.mtn");
			const char16_t *cn = keep.back().c_str();
			m[std::u16string(b) + u".mtn"] = Redirect{{nullptr, cn, cn}, 1};
		}
		static const char16_t *subs[] = {u"op", u"ed1_ena", u"ed2_anj", u"ed3_tsu", u"ed4_rir", u"ed5_mik",
			u"ed6_nay", u"edg_gra", u"mv1", u"mv2", u"mv3"};
		for (auto b : subs) {
			keep.push_back(std::u16string(b) + u"_chs.ass");
			const char16_t *chs = keep.back().c_str();
			keep.push_back(std::u16string(b) + u"_cht.ass");
			const char16_t *cht = keep.back().c_str();
			m[std::u16string(b) + u".ass"] = Redirect{{nullptr, chs, cht}, 0};
		}
		return m;
	}();
	return t;
}

// Language indexes as the DLL evaluates them. TJS may only run on the main
// thread; other threads (image prefetch, movie decoding) use the last value.
std::atomic<int> LastLang[2] = {{-1}, {-1}};

int LanguageIndex(int eval)
{
	if (std::this_thread::get_id() == MainThread) {
		static const ttstr expr[2] = {
			TJS_W("(typeof kag == \"Object\") ? kag.languageType : 0"),
			// The DLL evaluates plain SystemConfig.uiLanguageType when a file is opened. Lookups
			// here also come from existence checks, some while kag is being built; the
			// patch's getter returns 0 (no redirect) without kag, so skip it until then.
			TJS_W("(typeof kag == \"Object\" && typeof SystemConfig == \"Object\") ? SystemConfig.uiLanguageType : void"),
		};
		int idx = -1;
		try {
			tTJSVariant v;
			TVPExecuteExpression(expr[eval], &v);
			if (v.Type() == tvtInteger) idx = (int)(tjs_int)v;
		} catch (...) {
		}
		LastLang[eval] = idx;
		return idx;
	}
	return LastLang[eval];
}

bool IsBareName(const ttstr &name)
{
	for (const tjs_char *p = name.c_str(); *p; ++p)
		if (*p == TJS_W('/') || *p == TJS_W('\\') || *p == TJS_W(':') || *p == TJS_W('>')) return false;
	return !name.IsEmpty();
}

} // namespace

void LLLJZhInit()
{
	Ready = true;
	EnsurePack();
}

bool LLLJZhEnabled()
{
	if (!Ready) return false;
	EnsurePack();
	return (bool)Pack;
}

ttstr LLLJZhRedirect(const ttstr &leaf)
{
	if (!LLLJZhEnabled()) return leaf;
	auto &t = RedirectTable();
	auto it = t.find(Lower(leaf));
	if (it == t.end()) return leaf;
	int idx = LanguageIndex(it->second.eval);
	if (idx < 0 || idx > 2 || !it->second.map[idx]) return leaf;
	return ttstr(reinterpret_cast<const tjs_char *>(it->second.map[idx]));
}

int LLLJOverlayLookup(const ttstr &name, ttstr &out)
{
	if (!IsBareName(name) || !LLLJZhEnabled()) return 0;
	ttstr leaf = LLLJZhRedirect(name);
	if (FindEntry(leaf)) {
		out = ttstr(TJS_W("xpck://./")) + leaf;
		return 1;
	}
	if (leaf != name) {
		out = leaf;
		return 2;
	}
	return 0;
}

ttstr LLLJOverlayLeaf(const ttstr &leaf)
{
	if (leaf.IsEmpty() || !LLLJZhEnabled() || !FindEntry(leaf)) return ttstr();
	return ttstr(TJS_W("xpck://./")) + leaf;
}

bool LLLJZhReadFile(const ttstr &leaf, std::vector<unsigned char> &data)
{
	if (!LLLJZhEnabled()) return false;
	const XpckEntry *e = FindEntry(leaf);
	if (!e) return false;
	XpckStream s(Pack, *e);
	data.resize((size_t)e->size);
	return s.Read(data.data(), (tjs_uint)data.size()) == data.size();
}

// LLLJNative.zhEnabled(): the Chinese patch pack is mounted
LLLJ_NATIVE(zhEnabled)
{
	if (result) *result = (tjs_int)(LLLJZhEnabled() ? 1 : 0);
	return TJS_S_OK;
}

// LLLJNative.zhRedirect(name): the name the redirect table resolves to now
LLLJ_NATIVE(zhRedirect)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	if (result) *result = LLLJZhRedirect(ttstr(*param[0]));
	return TJS_S_OK;
}
