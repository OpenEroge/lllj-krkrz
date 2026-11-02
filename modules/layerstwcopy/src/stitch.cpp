// layerStwCopy.dll replacement: Layer.stitchWrappedCopy (prelude/layerstwcopy.tjs).
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "compat/compat.h"
#include "compat/layer.h"
#include <algorithm>
#include <vector>

using compat::LayerOf;

// Layer.stitchWrappedCopy member (layerStwCopy.dll), attached by prelude/layerstwcopy.tjs as
//   stitchWrappedCopy(dx, dy, dw, dh, src, sx, sy, sw, sh, arr)
// which calls CompatNative.stitchWrappedCopy(this, ...). Semantics read from the
// original DLL (copy loop at 0x10002cb0, row copy at 0x10002c50):
//   - the source rect (sx, sy, sw, sh) is clipped to the source image and the
//     destination rect (dx, dy, dw, dh) to the destination image; a negative
//     size means the whole image;
//   - with ox = dx - clipped.left and oy = dy - clipped.top, destination row r
//     reads source row (oy + r) mod sh, starting at column
//     (ox + arr[(oy + r) mod arr.count]) mod sw, and the source row repeats
//     (wraps) across the destination width.
// uisystem's SysTransitionEffect uses it to shear the 2-row wipe mask into a
// 256x512 diagonal tile.
COMPAT_NATIVE(stitchWrappedCopy)
{
	if (numparams < 10) return TJS_E_BADPARAMCOUNT;
	tTJSNI_BaseLayer *dst = LayerOf(param[0]);
	tTJSNI_BaseLayer *src = LayerOf(param[5]);
	tTVPBaseBitmap *dstImage = dst ? dst->GetMainImage() : nullptr;
	tTVPBaseBitmap *srcImage = src ? src->GetMainImage() : nullptr;
	if (result) *result = (tjs_int)1;
	if (!dstImage || !srcImage || !dstImage->Is32BPP() || !srcImage->Is32BPP()) return TJS_S_OK;

	auto clipped = [](tjs_int x, tjs_int y, tjs_int w, tjs_int h, tTVPBaseBitmap *image) {
		const tjs_int iw = (tjs_int)image->GetWidth(), ih = (tjs_int)image->GetHeight();
		if (w < 0) w = iw;
		if (h < 0) h = ih;
		tTVPRect r(x, y, x + w, y + h);
		if (!TVPIntersectRect(&r, r, tTVPRect(0, 0, iw, ih))) r = tTVPRect(0, 0, 0, 0);
		return r;
	};
	const tjs_int dx = (tjs_int)*param[1], dy = (tjs_int)*param[2];
	const tTVPRect s = clipped((tjs_int)*param[6], (tjs_int)*param[7], (tjs_int)*param[8], (tjs_int)*param[9], srcImage);
	const tTVPRect d = clipped(dx, dy, (tjs_int)*param[3], (tjs_int)*param[4], dstImage);
	const tjs_int sw = s.get_width(), sh = s.get_height(), dw = d.get_width();
	if (sw <= 0 || sh <= 0 || dw <= 0 || d.get_height() <= 0) return TJS_S_OK;

	std::vector<tjs_int> shifts;
	if (numparams >= 11 && param[10]->Type() == tvtObject && param[10]->AsObjectNoAddRef()) {
		tTJSVariantClosure arr = param[10]->AsObjectClosureNoAddRef();
		tTJSVariant count;
		if (TJS_SUCCEEDED(arr.PropGet(0, TJS_W("count"), nullptr, &count, nullptr))) {
			shifts.resize((tjs_int)count);
			for (size_t i = 0; i < shifts.size(); ++i) {
				tTJSVariant v;
				arr.PropGetByNum(0, (tjs_int)i, &v, nullptr);
				shifts[i] = v.Type() == tvtVoid ? 0 : (tjs_int)v;
			}
		}
	}
	auto mod = [](tjs_int a, tjs_int m) { a %= m; return a < 0 ? a + m : a; };
	const tjs_int ox = dx - d.left, oy = dy - d.top;
	for (tjs_int r = 0; r < d.get_height(); ++r) {
		const tjs_int k = oy + r;
		const tjs_int shift = shifts.empty() ? 0 : shifts[mod(k, (tjs_int)shifts.size())];
		const tjs_uint32 *row = (const tjs_uint32 *)srcImage->GetScanLine(s.top + mod(k, sh)) + s.left;
		tjs_uint32 *out = (tjs_uint32 *)dstImage->GetScanLineForWrite(d.top + r) + d.left;
		for (tjs_int x = 0, sx = mod(ox + shift, sw); x < dw;) {
			const tjs_int n = std::min(dw - x, sw - sx);
			std::copy(row + sx, row + sx + n, out + x);
			x += n;
			sx = 0;
		}
	}
	dst->SetImageModified(true);
	tTVPRect ur = d;
	ur.add_offsets(dst->GetImageLeft(), dst->GetImageTop()); // Update() takes layer coordinates
	dst->Update(ur);
	return TJS_S_OK;
}

