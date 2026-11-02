// WebP images stored under other image names (e.g. a .png that is really
// WebP, as some patch packs ship them). Registered as an engine graphic sniffer
// (EngineHooks.h): the PNG loader asks it first; anything that is not WebP goes
// on to libpng untouched.
#include "tjsCommHead.h"
#include "compat/compat.h"
#include "EngineHooks.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "tjsDictionary.h"
#include <webp/decode.h>
#include <cstring>
#include <vector>

namespace {

bool IsWebP(iTJSBinaryStream *src)
{
	unsigned char head[12];
	tjs_uint64 pos = src->Seek(0, TJS_BS_SEEK_CUR);
	tjs_uint n = src->Read(head, sizeof head);
	src->Seek(pos, TJS_BS_SEEK_SET);
	return n == sizeof head && !memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WEBP", 4);
}

std::vector<uint8_t> ReadAll(iTJSBinaryStream *src)
{
	tjs_uint64 pos = src->Seek(0, TJS_BS_SEEK_CUR);
	tjs_uint64 size = src->GetSize() - pos;
	std::vector<uint8_t> data((size_t)size);
	size_t got = 0;
	while (got < data.size()) {
		tjs_uint n = src->Read(data.data() + got, (tjs_uint)(data.size() - got));
		if (!n) break;
		got += n;
	}
	data.resize(got);
	return data;
}

void Fail(const char *what)
{
	TVPThrowExceptionMessage((ttstr(TJS_W("WebP decode error: ")) + ttstr(what)).c_str());
}

bool LoadWebP(tTVPGraphicSizeCallback sizecallback, tTVPGraphicScanLineCallback scanlinecallback,
	void *callbackdata, iTJSBinaryStream *src, tTVPGraphicLoadMode mode)
{
	if (!IsWebP(src)) return false;
	std::vector<uint8_t> data = ReadAll(src);
	int w = 0, h = 0;
	// glmNormalRGBA wants R G B A in memory; every other mode starts from B G R A
	uint8_t *rgba = mode == glmNormalRGBA ? WebPDecodeRGBA(data.data(), data.size(), &w, &h)
		: WebPDecodeBGRA(data.data(), data.size(), &w, &h);
	if (!rgba) Fail("invalid data");
	try {
		sizecallback(callbackdata, w, h);
		for (int y = 0; y < h; ++y) {
			auto *dst = static_cast<uint8_t *>(scanlinecallback(callbackdata, y));
			if (!dst) break;
			const uint8_t *row = rgba + (size_t)y * w * 4;
			if (mode == glmPalettized || mode == glmGrayscale) {
				// 8-bit luminance, as libpng's rgb_to_gray does for the PNG path
				for (int x = 0; x < w; ++x) {
					const uint8_t *p = row + x * 4; // B G R A
					dst[x] = (uint8_t)((p[0] * 29 + p[1] * 150 + p[2] * 77 + 128) >> 8);
				}
			} else {
				memcpy(dst, row, (size_t)w * 4);
			}
			scanlinecallback(callbackdata, -1);
		}
	} catch (...) {
		WebPFree(rgba);
		throw;
	}
	WebPFree(rgba);
	return true;
}

bool LoadHeaderWebP(iTJSBinaryStream *src, iTJSDispatch2 **dic)
{
	if (!IsWebP(src)) return false;
	std::vector<uint8_t> data = ReadAll(src);
	int w = 0, h = 0;
	if (!WebPGetInfo(data.data(), data.size(), &w, &h)) Fail("invalid header");
	*dic = TJSCreateDictionaryObject();
	tTJSVariant val((tjs_int64)w);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, *dic);
	val = tTJSVariant((tjs_int64)h);
	(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, *dic);
	return true;
}

} // namespace

static compat::RegisterInit CompatWebPInit(compat::OrderModule, "webp",
	[] { TVPAddGraphicSniffer({LoadWebP, LoadHeaderWebP}); });
