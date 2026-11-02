// msdfrender.dll replacement: the Layer members behind the MSDF/SDF atlas fonts.
//
// prerenderfontex.tjs (atlasDrawText, resizeDrawTextSDF) and embfontloader.tjs call
//   makeGlyphMSDF(param, x, y, w, h)   makeGlyphSDF(param, x, y, w, h [, province])
//   operateGlyphToProvince(x, y, w, h, bitmap)   makeBitmapFromProvince(x, y, w, h)
//   getAscentHeight()
// prelude.tjs attaches TJS wrappers (LLLJMsdfMembers) to Layer that call the natives
// below. Addresses are in the original msdfrender.dll (32-bit, V2Link at 0x10001500):
//   makeGlyphMSDF   wrapper 0x10004d60, mode 1 (ctor 0x100024a0, decoder 0x10002300)
//   makeGlyphSDF    wrapper 0x10004dc0, mode 2 (decoder 0x100023f0)
//   operateGlyphToProvince  0x10004f90, mode 5 (blit 0x10004e20)
//   makeBitmapFromProvince  0x100047c0, mode 3 (copy 0x100022b0)
//   getAscentHeight         0x10004620 (font metrics helper 0x100056e0, GDI there)
// The glyph natives return or consume a glyph in the format Layer.drawGlyph reads
// (tTVPNativeBaseBitmap::DrawGlyph): [width, height, octet of width*height bytes],
// packed rows, 8-bit coverage.
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "FontRasterizer.h"
#include "lllj_compat.h"

#include <algorithm>
#include <vector>

namespace {

// The engine's layer behind a TJS Layer object, or null for anything else.
tTJSNI_BaseLayer *LayerOf(tTJSVariant *v)
{
	if (v->Type() != tvtObject) return nullptr;
	iTJSDispatch2 *obj = v->AsObjectNoAddRef();
	tTJSNI_BaseLayer *layer = nullptr;
	if (!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
			tTJSNC_Layer::ClassID, (iTJSNativeInstance **)&layer)))
		return nullptr;
	return layer;
}

// Median of three channel values: min(max(a,b), max(min(a,b), c)) as in the DLL's
// decoder at 0x10002300.
tjs_int Median3(tjs_int a, tjs_int b, tjs_int c)
{
	return std::min(std::max(a, b), std::max(std::min(a, b), c));
}

// Coverage of one sample. The DLL computes min(sd * P + T, 0xffffff) >> 16 with
// P = (int)(param * 65536) and T = (65536 - P) << 7, so param scales the distance
// around the 128 edge: cov = 128 + (sd - 128) * param. The DLL leaves negative sums
// (param > 1) unclamped; they are clamped to 0 here.
tjs_uint8 Coverage(tjs_int sd, tjs_int64 scale, tjs_int64 offset)
{
	tjs_int64 v = (tjs_int64)sd * scale + offset;
	if (v < 0) v = 0;
	if (v > 0xffffff) v = 0xffffff;
	return (tjs_uint8)(v >> 16);
}

// [width, height, octet] for a packed width*height coverage buffer.
void SetGlyphResult(tTJSVariant *result, tjs_int w, tjs_int h, const std::vector<tjs_uint8> &bitmap)
{
	iTJSDispatch2 *array = TJSCreateArrayObject();
	tTJSVariant r(array, array);
	array->Release();
	tTJSVariant vw((tjs_int)w), vh((tjs_int)h), octet(bitmap.data(), (tjs_uint)bitmap.size());
	array->PropSetByNum(TJS_MEMBERENSURE, 0, &vw, array);
	array->PropSetByNum(TJS_MEMBERENSURE, 1, &vh, array);
	array->PropSetByNum(TJS_MEMBERENSURE, 2, &octet, array);
	if (result) *result = r;
}

} // namespace

// Layer.makeGlyphMSDF / Layer.makeGlyphSDF, attached by prelude.tjs as
//   msdfMakeGlyph(this, sdf, param, x, y, w, h, province)
// Reads the rectangle (x, y, x+w, y+h) of the layer's main image and returns the
// coverage glyph, or void when the rectangle is empty or the image is missing.
//   MSDF (sdf == false): sd = median of the R, G, B bytes (0x10002300).
//   SDF  (sdf == true):  sd = alpha byte (0x100023f0, the DLL adds 3 to the pixel).
// The SDF source becomes the province image (8-bit) when `province` is given non-zero,
// or when no argument is given and the layer face is dfProvince (3), which is the
// DLL's rule at 0x1000262d-0x1000268f. Pixels outside the image read as zero.
LLLJ_NATIVE(msdfMakeGlyph)
{
	if (numparams < 7) return TJS_E_BADPARAMCOUNT;
	if (result) result->Clear();
	tTJSNI_BaseLayer *layer = LayerOf(param[0]);
	if (!layer) return TJS_S_OK;
	const bool sdf = param[1]->operator bool();
	const tjs_real p = (tjs_real)*param[2];
	const tjs_int x = (tjs_int)*param[3], y = (tjs_int)*param[4];
	const tjs_int w = (tjs_int)*param[5], h = (tjs_int)*param[6];
	if (w <= 0 || h <= 0) return TJS_S_OK;

	bool province = false;
	if (sdf) {
		if (numparams >= 8 && param[7]->Type() != tvtVoid) province = param[7]->operator bool();
		else province = layer->GetFace() == dfProvince;
	}
	tTVPBaseBitmap *img = province ? layer->GetProvinceImage() : layer->GetMainImage();
	if (!img || (province ? img->GetBPP() != 8 : !img->Is32BPP())) return TJS_S_OK;
	const tjs_int iw = (tjs_int)img->GetWidth(), ih = (tjs_int)img->GetHeight();

	// P = (int)(param * 65536) and T = (65536 - P) * 128. Params outside [-256, 256]
	// are not meaningful; clamping keeps the 64-bit arithmetic in range.
	const tjs_int64 scale = (tjs_int64)(std::max(-256.0, std::min(256.0, p)) * 65536.0);
	const tjs_int64 offset = (65536 - scale) * 128;
	std::vector<tjs_uint8> bitmap((size_t)w * (size_t)h);
	for (tjs_int j = 0; j < h; ++j) {
		const tjs_int sy = y + j;
		const tjs_uint8 *row = (sy >= 0 && sy < ih) ? (const tjs_uint8 *)img->GetScanLine((tjs_uint)sy) : nullptr;
		for (tjs_int i = 0; i < w; ++i) {
			const tjs_int sx = x + i;
			tjs_int sd = 0;
			if (row && sx >= 0 && sx < iw) {
				if (province) sd = row[sx];
				else if (sdf) sd = row[sx * 4 + 3];
				else sd = Median3(row[sx * 4 + 2], row[sx * 4 + 1], row[sx * 4]);
			}
			bitmap[(size_t)j * w + i] = Coverage(sd, scale, offset);
		}
	}
	SetGlyphResult(result, w, h, bitmap);
	return TJS_S_OK;
}

// Layer.makeBitmapFromProvince(x, y, w, h) -> [w, h, octet]: a plain copy of the
// province rectangle (0x100022b0). Void when there is no province image.
LLLJ_NATIVE(msdfBitmapFromProvince)
{
	if (numparams < 5) return TJS_E_BADPARAMCOUNT;
	if (result) result->Clear();
	tTJSNI_BaseLayer *layer = LayerOf(param[0]);
	tTVPBaseBitmap *img = layer ? layer->GetProvinceImage() : nullptr;
	if (!img || img->GetBPP() != 8) return TJS_S_OK;
	const tjs_int iw = (tjs_int)img->GetWidth(), ih = (tjs_int)img->GetHeight();
	const tjs_int x = (tjs_int)*param[1], y = (tjs_int)*param[2];
	const tjs_int w = (tjs_int)*param[3], h = (tjs_int)*param[4];
	if (w <= 0 || h <= 0) return TJS_S_OK;

	std::vector<tjs_uint8> bitmap((size_t)w * (size_t)h, 0);
	for (tjs_int j = 0; j < h; ++j) {
		const tjs_int sy = y + j;
		if (sy < 0 || sy >= ih) continue;
		const tjs_uint8 *row = (const tjs_uint8 *)img->GetScanLine((tjs_uint)sy);
		for (tjs_int i = 0; i < w; ++i) {
			const tjs_int sx = x + i;
			if (sx >= 0 && sx < iw) bitmap[(size_t)j * w + i] = row[sx];
		}
	}
	SetGlyphResult(result, w, h, bitmap);
	return TJS_S_OK;
}

// Layer.operateGlyphToProvince(x, y, w, h, octet): province[x.., y..] = min(255,
// province + glyph), a saturating add of the coverage (inner loop of 0x10004e20).
// The octet must hold at least w*h bytes, as the DLL checks.
LLLJ_NATIVE(msdfOperateProvince)
{
	if (numparams < 6) return TJS_E_BADPARAMCOUNT;
	if (result) result->Clear();
	tTJSNI_BaseLayer *layer = LayerOf(param[0]);
	tTVPBaseBitmap *img = layer ? layer->GetProvinceImage() : nullptr;
	if (!img || img->GetBPP() != 8 || param[5]->Type() != tvtOctet) return TJS_S_OK;
	const tTJSVariantOctet *octet = param[5]->AsOctetNoAddRef();
	const tjs_int x = (tjs_int)*param[1], y = (tjs_int)*param[2];
	const tjs_int w = (tjs_int)*param[3], h = (tjs_int)*param[4];
	if (w <= 0 || h <= 0 || octet->GetLength() < (tjs_uint)w * (tjs_uint)h) return TJS_S_OK;
	const tjs_uint8 *src = octet->GetData();
	const tjs_int iw = (tjs_int)img->GetWidth(), ih = (tjs_int)img->GetHeight();

	for (tjs_int j = 0; j < h; ++j) {
		const tjs_int dy = y + j;
		if (dy < 0 || dy >= ih) continue;
		tjs_uint8 *row = (tjs_uint8 *)img->GetScanLineForWrite((tjs_uint)dy);
		for (tjs_int i = 0; i < w; ++i) {
			const tjs_int dx = x + i;
			if (dx < 0 || dx >= iw) continue;
			row[dx] = (tjs_uint8)std::min<tjs_int>(255, row[dx] + src[(size_t)j * w + i]);
		}
	}
	return TJS_S_OK;
}

// Layer.getAscentHeight(): the ascent of the layer's current font, in pixels. The
// original asks GDI (font metrics helper 0x100056e0); this port uses the engine's
// rasterizer, the same value the layer's own text baseline uses (LayerBitmapImpl.cpp).
LLLJ_NATIVE(msdfAscentHeight)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	if (result) result->Clear();
	tTJSNI_BaseLayer *layer = LayerOf(param[0]);
	tTVPBaseBitmap *img = layer ? layer->GetMainImage() : nullptr;
	if (!img) return TJS_S_OK;
	FontRasterizer *rasterizer = GetCurrentRasterizer();
	rasterizer->ApplyFont(img, true);
	if (result) *result = (tjs_int)rasterizer->GetAscentHeight();
	return TJS_S_OK;
}
