// Natives behind members that the Windows plugins provided and the game calls
// without guards. prelude.tjs installs them:
//   Layer.stitchWrappedCopy    layerStwCopy.dll (uisystem's wipe transition)
//   Layer.fetchImageSize       PackinOne
//   Storages.saveOctet         PackinOne
//   System.getKnownFolderPath  PackinOne (snapcapture's "Captures" folder)
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "CharacterSet.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "lllj_compat.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

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

// XDG pictures directory (user-dirs.dirs, default ~/Pictures) if it exists, else $HOME.
std::string PicturesDir()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home) return std::string();
	const std::string h = home;
	std::string dir = h + "/Pictures";
	const char *config = std::getenv("XDG_CONFIG_HOME");
	std::ifstream in((config && *config ? std::string(config) : h + "/.config") + "/user-dirs.dirs");
	const std::string key = "XDG_PICTURES_DIR=\"";
	for (std::string line; std::getline(in, line);) {
		if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 || line.back() != '"')
			continue;
		std::string value = line.substr(key.size(), line.size() - key.size() - 1);
		if (value == "$HOME") value = h;
		else if (value.compare(0, 6, "$HOME/") == 0) value = h + value.substr(5);
		dir = value;
		break;
	}
	struct stat st;
	return (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? dir : h;
}

} // namespace

// Layer.stitchWrappedCopy member (layerStwCopy.dll), attached by prelude.tjs as
//   stitchWrappedCopy(dx, dy, dw, dh, src, sx, sy, sw, sh, arr)
// which calls LLLJNative.stitchWrappedCopy(this, ...). Semantics read from the
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
LLLJ_NATIVE(stitchWrappedCopy)
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

// Layer.fetchImageSize(storage) -> [width, height]. Void when the image cannot be
// loaded, which is what the callers test for. The image is decoded by
// TVPLoadGraphic, the path Layer.loadImages uses, so names resolve identically
// (auto path, missing extension).
LLLJ_NATIVE(fetchImageSize)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	tTVPBaseBitmap bitmap(1, 1, 32); // the loader sizes it to the image
	try {
		TVPLoadGraphic(&bitmap, ttstr(*param[0]), TVP_clNone, 0, 0, glmNormal);
	} catch (...) {
		if (result) result->Clear(); // void, not the VM's default integer
		return TJS_S_OK;
	}
	iTJSDispatch2 *array = TJSCreateArrayObject();
	tTJSVariant r(array, array);
	array->Release();
	tTJSVariant w((tjs_int)bitmap.GetWidth()), h((tjs_int)bitmap.GetHeight());
	array->PropSetByNum(TJS_MEMBERENSURE, 0, &w, array);
	array->PropSetByNum(TJS_MEMBERENSURE, 1, &h, array);
	if (result) *result = r;
	return TJS_S_OK;
}

// Storages.saveOctet(storage, octet): replaces the storage with the octet's bytes.
LLLJ_NATIVE(saveOctet)
{
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	if (param[1]->Type() != tvtOctet) TVPThrowExceptionMessage(TJS_W("saveOctet: octet expected"));
	tTJSVariantOctet *octet = param[1]->AsOctetNoAddRef();
	std::unique_ptr<tTJSBinaryStream> s(TVPCreateStream(ttstr(*param[0]), TJS_BS_WRITE));
	if (octet->GetLength())
		TJS::TVPWriteBuffer(s.get(), octet->GetData(), (tjs_uint)octet->GetLength());
	return TJS_S_OK;
}

// System.getKnownFolderPath(name, flags) -> folder path, or "" when unknown (the
// game's failure value). Flags are ignored; only "Captures" is mapped.
LLLJ_NATIVE(getKnownFolderPath)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	std::string dir;
	if (ttstr(*param[0]) == TJS_W("Captures")) dir = PicturesDir();
	tjs_string wide;
	TVPUtf8ToUtf16(wide, dir);
	if (result) *result = ttstr(wide.c_str());
	return TJS_S_OK;
}
