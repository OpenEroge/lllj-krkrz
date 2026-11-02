// PackinOne tjsDataPack replacement:
//   Scripts.loadDataPack(storage)        -> value     (TJS/4s0 and TJS/ns0)
//   Scripts.saveDataPack(storage, value)              (writes plain TJS/ns0)
//
// Format notes (re-implemented from the analysis in lifegpc/msg-tool, see
// tools/pbd_decode.py for the Python reference that was validated on every
// .pbd of the game):
//   header : "TJS/" "4s0\0"|"ns0\0" | u32 seed | u16 crypt | u16 iv_len | iv
//   4s0    : optional ChaCha stream (key = keyed BLAKE2s-256(key = seed LE,
//            msg = iv), nonce = (xxh32(iv, seed), seed)), then framed LZ4
//            [u16 csize][block]..., blocks may reference prior output.
//   value  : u16 (check << 8 | type): 0 void, 2 string(u32 n, utf16le),
//            4 int64, 5 double, 0x81 array, 0xC1 dictionary; u32 trailer.
#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "StorageIntf.h"
#include "MsgIntf.h"
#include "lllj_compat.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

typedef std::vector<uint8_t> Bytes;

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// ---------------------------------------------------------------- xxh32
uint32_t xxh32(const uint8_t *p, size_t n, uint32_t seed)
{
	const uint32_t P1 = 2654435761u, P2 = 2246822519u, P3 = 3266489917u, P4 = 668265263u, P5 = 374761393u;
	size_t i = 0;
	uint32_t h;
	if (n >= 16) {
		uint32_t v[4] = { seed + P1 + P2, seed + P2, seed, seed - P1 };
		while (i + 16 <= n) {
			for (int k = 0; k < 4; k++) v[k] = rotl32(v[k] + rd32(p + i + 4 * k) * P2, 13) * P1;
			i += 16;
		}
		h = rotl32(v[0], 1) + rotl32(v[1], 7) + rotl32(v[2], 12) + rotl32(v[3], 18);
	} else {
		h = seed + P5;
	}
	h += (uint32_t)n;
	for (; i + 4 <= n; i += 4) h = rotl32(h + rd32(p + i) * P3, 17) * P4;
	for (; i < n; i++) h = rotl32(h + p[i] * P5, 11) * P1;
	h ^= h >> 15; h *= P2; h ^= h >> 13; h *= P3; h ^= h >> 16;
	return h;
}

// ---------------------------------------------------------------- BLAKE2s-256 (keyed)
struct Blake2s {
	uint32_t h[8], t[2] = {0, 0}, f[2] = {0, 0};
	uint8_t buf[64];
	size_t buflen = 0;
	static constexpr uint32_t IV[8] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
		0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
	static constexpr uint8_t SIGMA[10][16] = {
		{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
		{11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4}, {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
		{9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13}, {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
		{12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11}, {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
		{6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5}, {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0} };

	Blake2s(const uint8_t *key, size_t keylen)
	{
		for (int i = 0; i < 8; i++) h[i] = IV[i];
		h[0] ^= 0x01010000u ^ ((uint32_t)keylen << 8) ^ 32u;
		if (keylen) {
			uint8_t block[64] = {0};
			memcpy(block, key, keylen);
			update(block, 64);
		}
	}
	void compress(const uint8_t *block, bool last)
	{
		uint32_t m[16], v[16];
		for (int i = 0; i < 16; i++) m[i] = rd32(block + 4 * i);
		for (int i = 0; i < 8; i++) { v[i] = h[i]; v[i + 8] = IV[i]; }
		v[12] ^= t[0]; v[13] ^= t[1];
		if (last) v[14] = ~v[14];
		auto G = [&](int r, int i, int a, int b, int c, int d) {
			v[a] = v[a] + v[b] + m[SIGMA[r][2 * i]]; v[d] = rotr32(v[d] ^ v[a], 16);
			v[c] = v[c] + v[d]; v[b] = rotr32(v[b] ^ v[c], 12);
			v[a] = v[a] + v[b] + m[SIGMA[r][2 * i + 1]]; v[d] = rotr32(v[d] ^ v[a], 8);
			v[c] = v[c] + v[d]; v[b] = rotr32(v[b] ^ v[c], 7);
		};
		for (int r = 0; r < 10; r++) {
			G(r, 0, 0, 4, 8, 12); G(r, 1, 1, 5, 9, 13); G(r, 2, 2, 6, 10, 14); G(r, 3, 3, 7, 11, 15);
			G(r, 4, 0, 5, 10, 15); G(r, 5, 1, 6, 11, 12); G(r, 6, 2, 7, 8, 13); G(r, 7, 3, 4, 9, 14);
		}
		for (int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[i + 8];
	}
	void incr(uint32_t n) { t[0] += n; if (t[0] < n) t[1]++; }
	void update(const uint8_t *p, size_t n)
	{
		while (n > 0) {
			if (buflen == 64) { incr(64); compress(buf, false); buflen = 0; }
			size_t k = std::min(n, 64 - buflen);
			memcpy(buf + buflen, p, k);
			buflen += k; p += k; n -= k;
		}
	}
	void final(uint8_t out[32])
	{
		incr((uint32_t)buflen);
		memset(buf + buflen, 0, 64 - buflen);
		compress(buf, true);
		for (int i = 0; i < 8; i++) wr32(out + 4 * i, h[i]);
	}
};
constexpr uint32_t Blake2s::IV[8];
constexpr uint8_t Blake2s::SIGMA[10][16];

// ---------------------------------------------------------------- ChaCha stream
void ChaChaBlock(const uint8_t key[32], uint32_t nlo, uint32_t nhi, uint64_t counter, int rounds, uint32_t out[16])
{
	uint32_t st[16] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574 };
	for (int i = 0; i < 8; i++) st[4 + i] = rd32(key + 4 * i);
	st[12] = (uint32_t)counter; st[13] = (uint32_t)(counter >> 32); st[14] = nlo; st[15] = nhi;
	uint32_t x[16];
	memcpy(x, st, sizeof(x));
	auto QR = [&](int a, int b, int c, int d) {
		x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16);
		x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12);
		x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8);
		x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);
	};
	for (int r = 0; r < rounds; r += 2) {
		QR(0, 4, 8, 12); QR(1, 5, 9, 13); QR(2, 6, 10, 14); QR(3, 7, 11, 15);
		QR(0, 5, 10, 15); QR(1, 6, 11, 12); QR(2, 7, 8, 13); QR(3, 4, 9, 14);
	}
	for (int i = 0; i < 16; i++) out[i] = x[i] + st[i];
}

void Decrypt(Bytes &body, uint32_t seed, const uint8_t *iv, size_t ivlen, int field)
{
	int rounds, p4;
	switch (field) {
	case 1: rounds = 8; p4 = 16; break;
	case 2: rounds = 12; p4 = 8; break;
	case 3: rounds = 20; p4 = 4; break;
	case 4: rounds = 8; p4 = 1; break;
	case 5: rounds = 12; p4 = 1; break;
	case 6: rounds = 20; p4 = 1; break;
	default: throw std::runtime_error("unsupported datapack crypt field");
	}
	uint8_t seedle[4];
	wr32(seedle, seed);
	uint8_t key[32];
	Blake2s b(seedle, 4);
	b.update(iv, ivlen);
	b.final(key);
	uint32_t nlo = xxh32(iv, ivlen, seed), nhi = seed;
	uint32_t fb = nhi ^ nlo;
	if (!fb) fb = seed ? seed : 0xffffffffu;
	std::vector<uint32_t> words;
	size_t pos = 0;
	uint64_t bc = 0;
	while (pos < body.size()) {
		uint32_t blk[16];
		ChaChaBlock(key, nlo, nhi, bc++, rounds, blk);
		words.assign(blk, blk + 16);
		if (p4 > 1) {
			size_t src = 0;
			for (int g = 0; g < p4 * 4 - 4; g++)
				for (int k = 0; k < 4; k++) {
					uint32_t w = words[src++];
					w ^= w << 13; w ^= w >> 17; w ^= w << 5;
					words.push_back(w ? w : fb);
				}
		}
		size_t kslen = words.size() * 4;
		for (size_t i = 0; i < kslen && pos < body.size(); i++, pos++)
			body[pos] ^= (uint8_t)(words[i >> 2] >> ((i & 3) * 8));
	}
}

// ---------------------------------------------------------------- LZ4 (framed blocks)
void Lz4Block(const uint8_t *src, size_t n, Bytes &out)
{
	size_t i = 0;
	while (i < n) {
		uint8_t tok = src[i++];
		size_t lit = tok >> 4;
		if (lit == 15) { uint8_t b; do { if (i >= n) throw std::runtime_error("lz4 overrun"); b = src[i++]; lit += b; } while (b == 255); }
		if (i + lit > n) throw std::runtime_error("lz4 literal overrun");
		out.insert(out.end(), src + i, src + i + lit);
		i += lit;
		if (i >= n) break;
		if (i + 2 > n) throw std::runtime_error("lz4 offset overrun");
		size_t off = src[i] | (src[i + 1] << 8);
		i += 2;
		size_t ml = tok & 15;
		if (ml == 15) { uint8_t b; do { if (i >= n) throw std::runtime_error("lz4 overrun"); b = src[i++]; ml += b; } while (b == 255); }
		ml += 4;
		if (off == 0 || off > out.size()) throw std::runtime_error("lz4 bad offset");
		size_t start = out.size() - off;
		for (size_t k = 0; k < ml; k++) out.push_back(out[start + k]);
	}
}

Bytes Lz4Frames(const Bytes &data)
{
	Bytes out;
	size_t pos = 0;
	while (pos + 2 <= data.size()) {
		size_t n = data[pos] | (data[pos + 1] << 8);
		if (n == 0 || pos + 2 + n > data.size()) break;
		Lz4Block(data.data() + pos + 2, n, out);
		pos += 2 + n;
	}
	return out;
}

// ---------------------------------------------------------------- value tree
struct Reader {
	const Bytes &b;
	size_t p = 0;
	explicit Reader(const Bytes &bb) : b(bb) {}
	void need(size_t n) { if (p + n > b.size()) throw std::runtime_error("datapack truncated"); }
	uint32_t u32() { need(4); uint32_t v = rd32(&b[p]); p += 4; return v; }
	ttstr str()
	{
		uint32_t n = u32();
		need((size_t)n * 2);
		std::u16string s(n, u'\0');
		for (uint32_t i = 0; i < n; i++) s[i] = (char16_t)(b[p + 2 * i] | (b[p + 2 * i + 1] << 8));
		p += (size_t)n * 2;
		return ttstr((const tjs_char *)s.c_str());
	}
	tTJSVariant value(int depth = 0)
	{
		if (depth > 256) throw std::runtime_error("datapack too deep");
		need(2);
		uint8_t t = b[p];
		p += 2;
		switch (t) {
		case 0: return tTJSVariant();
		case 2: return tTJSVariant(str());
		case 4: { need(8); int64_t v; memcpy(&v, &b[p], 8); p += 8; return tTJSVariant((tjs_int64)v); }
		case 5: { need(8); double v; memcpy(&v, &b[p], 8); p += 8; return tTJSVariant((tjs_real)v); }
		case 0x81: {
			uint32_t n = u32();
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			for (uint32_t i = 0; i < n; i++) {
				tTJSVariant v = value(depth + 1);
				arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &v, arr);
			}
			tTJSVariant r(arr, arr);
			arr->Release();
			return r;
		}
		case 0xC1: {
			uint32_t n = u32();
			iTJSDispatch2 *dict = TJSCreateDictionaryObject();
			for (uint32_t i = 0; i < n; i++) {
				ttstr k = str();
				tTJSVariant v = value(depth + 1);
				dict->PropSet(TJS_MEMBERENSURE, k.c_str(), nullptr, &v, dict);
			}
			tTJSVariant r(dict, dict);
			dict->Release();
			return r;
		}
		default:
			throw std::runtime_error("datapack: unknown value type");
		}
	}
};

Bytes ReadStorage(const ttstr &name)
{
	std::unique_ptr<tTJSBinaryStream> s(TVPCreateStream(name, TJS_BS_READ));
	Bytes buf((size_t)s->GetSize());
	if (!buf.empty()) TJS::TVPReadBuffer(s.get(), buf.data(), (tjs_uint)buf.size());
	return buf;
}

tTJSVariant DecodeDataPack(const Bytes &all)
{
	// Windows saves embed the pack in consecutive JPEG APP11 segments. Each
	// segment holds up to 16KiB of decoded bytes; FF is escaped as FF 00 even
	// inside APP11. Plain ns0 saves from this port append after the thumbnail.
	Bytes embedded;
	if (all.size() >= 2 && all[0] == 0xff && all[1] == 0xd8) {
		bool collecting = false;
		for (size_t pos = 2; pos + 1 < all.size();) {
			if (all[pos] != 0xff) { ++pos; continue; } // scan entropy bytes
			const uint8_t marker = all[pos + 1];
			if (marker == 0xff) { ++pos; continue; } // marker fill
			if (marker == 0xd9) break;
			if (marker == 0 || marker == 1 || (marker >= 0xd0 && marker <= 0xd8)) { pos += 2; continue; }
			if (pos + 4 > all.size()) throw std::runtime_error("JPEG marker truncated");
			const size_t length = (size_t(all[pos + 2]) << 8) | all[pos + 3];
			if (length < 2 || length > all.size() - pos - 2)
				throw std::runtime_error("JPEG segment truncated");
			const size_t begin = pos + 4, end = pos + 2 + length;
			if (marker == 0xeb) {
				if (!collecting && end - begin >= 8 && !memcmp(all.data() + begin, "TJS/", 4))
					collecting = true;
				if (collecting) {
					for (size_t i = begin; i < end; ++i) {
						embedded.push_back(all[i]);
						if (all[i] == 0xff && i + 1 < end && all[i + 1] == 0) ++i;
					}
				}
			}
			pos = end;
		}
	}
	const Bytes &container = embedded.empty() ? all : embedded;
	// Otherwise find an unwrapped pack (including our appended JPEG/PNG form).
	size_t start = 0;
	for (size_t i = 0; i + 8 <= container.size(); i++) {
		if (container[i] == 'T' && memcmp(&container[i], "TJS/", 4) == 0 && (container[i + 4] == '4' || container[i + 4] == 'n') &&
			container[i + 5] == 's' && container[i + 6] == '0' && container[i + 7] == 0) { start = i; break; }
	}
	const Bytes buf(container.begin() + start, container.end());
	if (buf.size() < 16 || memcmp(buf.data(), "TJS/", 4) != 0 || buf[5] != 's' || buf[6] != '0' || buf[7] != 0)
		throw std::runtime_error("not a TJS data pack");
	uint32_t seed = rd32(&buf[8]);
	int crypt = buf[12] | (buf[13] << 8);
	size_t ivlen = buf[14] | (buf[15] << 8);
	if (16 + ivlen > buf.size()) throw std::runtime_error("datapack iv truncated");
	Bytes body(buf.begin() + 16 + ivlen, buf.end());
	if (buf[4] == '4') {
		if (crypt) Decrypt(body, seed, buf.data() + 16, ivlen, crypt);
		body = Lz4Frames(body);
	} else if (buf[4] != 'n' || crypt != 0) {
		throw std::runtime_error("unsupported data pack variant");
	}
	Reader r(body);
	return r.value();
}

// ---------------------------------------------------------------- writer (ns0)
struct Writer {
	Bytes out;
	void u16(uint16_t v) { out.push_back(v); out.push_back(v >> 8); }
	void u32(uint32_t v) { uint8_t b[4]; wr32(b, v); out.insert(out.end(), b, b + 4); }
	void str(const ttstr &s)
	{
		tjs_int n = s.GetLen();
		u32((uint32_t)n);
		const tjs_char *c = s.c_str();
		for (tjs_int i = 0; i < n; i++) u16((uint16_t)c[i]);
	}
	void value(const tTJSVariant &v, int depth = 0);
};

class tEnumToWriter : public tTJSDispatch
{
public:
	Writer *W; int Depth; tjs_int Count = 0; bool Counting = true;
	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override
	{
		if (numparams >= 3) {
			tjs_uint32 flags = (tjs_int)*param[1];
			if (!(flags & TJS_HIDDENMEMBER)) {
				if (Counting) Count++;
				else { W->str(ttstr(*param[0])); W->value(*param[2], Depth + 1); }
			}
		}
		if (result) *result = (tjs_int)1;
		return TJS_S_OK;
	}
};

void Writer::value(const tTJSVariant &v, int depth)
{
	if (depth > 256) throw std::runtime_error("datapack too deep");
	switch (v.Type()) {
	case tvtVoid: u16(0); return;
	case tvtString: u16(2); str(ttstr(v)); return;
	case tvtInteger: { u16(4); int64_t i = (tjs_int64)v; uint8_t b[8]; memcpy(b, &i, 8); out.insert(out.end(), b, b + 8); return; }
	case tvtReal: { u16(5); double d = (tjs_real)v; uint8_t b[8]; memcpy(b, &d, 8); out.insert(out.end(), b, b + 8); return; }
	case tvtObject: {
		iTJSDispatch2 *obj = v.AsObjectNoAddRef();
		if (!obj) { u16(0); return; }
		if (obj->IsInstanceOf(0, nullptr, nullptr, TJS_W("Array"), obj) == TJS_S_TRUE) {
			tTJSVariant cnt;
			obj->PropGet(0, TJS_W("count"), nullptr, &cnt, obj);
			tjs_int n = (tjs_int)cnt;
			u16(0x81); u32((uint32_t)n);
			for (tjs_int i = 0; i < n; i++) {
				tTJSVariant e;
				obj->PropGetByNum(TJS_IGNOREPROP, i, &e, obj);
				value(e, depth + 1);
			}
			return;
		}
		tEnumToWriter *cb = new tEnumToWriter();
		cb->W = this; cb->Depth = depth;
		tTJSVariantClosure clo(cb, nullptr);
		obj->EnumMembers(TJS_IGNOREPROP, &clo, obj);
		u16(0xC1); u32((uint32_t)cb->Count);
		cb->Counting = false;
		obj->EnumMembers(TJS_IGNOREPROP, &clo, obj);
		cb->Release();
		return;
	}
	default:
		u16(0); return; // octets etc. are not representable
	}
}

} // namespace

LLLJ_NATIVE(loadDataPack)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	ttstr name = *param[0];
	try {
		tTJSVariant v = DecodeDataPack(ReadStorage(name));
		if (result) *result = v;
	} catch (const std::exception &e) {
		TVPThrowExceptionMessage((ttstr(TJS_W("loadDataPack: ")) + name + TJS_W(": ") + ttstr(e.what())).c_str());
	}
	return TJS_S_OK;
}

LLLJ_NATIVE(saveDataPack)
{
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	ttstr name = *param[0];
	Writer w;
	if (numparams >= 4 && param[3]->Type() == tvtOctet) {
		tTJSVariantOctet *o = param[3]->AsOctetNoAddRef();
		if (o && o->GetLength()) w.out.insert(w.out.end(), o->GetData(), o->GetData() + o->GetLength());
	}
	const char hdr[16] = { 'T','J','S','/','n','s','0','\0', 'T','J','S','\0', 0,0, 0,0 };
	w.out.insert(w.out.end(), hdr, hdr + 16);
	w.value(*param[1]);
	w.u32(0);
	std::unique_ptr<tTJSBinaryStream> s(TVPCreateStream(name, TJS_BS_WRITE));
	TJS::TVPWriteBuffer(s.get(), w.out.data(), (tjs_uint)w.out.size());
	if (result) *result = (tjs_int)1;
	return TJS_S_OK;
}

// Scripts.makeDataPackDigest(data, tick, id) -> seed stored in the save options.
// The original derives a digest for its encrypted save format; our ns0 saves only
// need a stable integer.
LLLJ_NATIVE(makeDataPackDigest)
{
	uint32_t a = numparams >= 2 ? (uint32_t)(tjs_int64)*param[1] : 0;
	ttstr id = numparams >= 3 && param[2]->Type() == tvtString ? ttstr(*param[2]) : ttstr();
	std::u16string s((const char16_t *)id.c_str());
	uint32_t h = xxh32((const uint8_t *)s.data(), s.size() * 2, a);
	if (result) *result = (tjs_int64)h;
	return TJS_S_OK;
}

// LLLJNative.readOctet(storage) -> octet with the storage's bytes
LLLJ_NATIVE(readOctet)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	Bytes b = ReadStorage(ttstr(*param[0]));
	if (result) *result = tTJSVariant(b.data(), (tjs_uint)b.size());
	return TJS_S_OK;
}

// Hxv4 hashes a leaf name, including extension, followed by its domain, in
// UTF-16LE. The unpacker stores the 256-bit digest as 64 uppercase hex digits.
ttstr LLLJHxFilename(const ttstr &leaf)
{
	ttstr input = leaf.AsLowerCase() + TJS_W("xp3hnp");
	Bytes encoded;
	for (const tjs_char *p = input.c_str(); *p; ++p) {
		encoded.push_back(*p & 255); encoded.push_back((*p >> 8) & 255);
	}
	Blake2s hash(nullptr, 0);
	hash.update(encoded.data(), encoded.size());
	uint8_t digest[32]; hash.final(digest);
	tjs_char text[65]; const tjs_char *hex = TJS_W("0123456789ABCDEF");
	for (int i = 0; i < 32; ++i) { text[i*2] = hex[digest[i] >> 4]; text[i*2+1] = hex[digest[i] & 15]; }
	text[64] = 0;
	return ttstr(text);
}
LLLJ_NATIVE(hashFilename)
{
	if (!numparams) return TJS_E_BADPARAMCOUNT;
	if (result) *result = LLLJHxFilename(ttstr(*param[0]));
	return TJS_S_OK;
}
