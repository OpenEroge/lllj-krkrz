#include "tjsCommHead.h"
#include "tjsNative.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "compat/layer.h"

tTJSNI_BaseLayer *compat::LayerOf(tTJSVariant *v)
{
	if (v->Type() != tvtObject) return nullptr;
	iTJSDispatch2 *obj = v->AsObjectNoAddRef();
	tTJSNI_BaseLayer *layer = nullptr;
	if (!obj || TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
			tTJSNC_Layer::ClassID, (iTJSNativeInstance **)&layer)))
		return nullptr;
	return layer;
}

bool Layer_FetchImageSize(ttstr imageName, int &width, int &height)
{
	tTVPBaseBitmap bitmap(1, 1, 32); // the loader sizes it to the image
	try {
		TVPLoadGraphic(&bitmap, imageName, TVP_clNone, 0, 0, glmNormal);
	} catch (...) {
		return false;
	}
	width = (int)bitmap.GetWidth();
	height = (int)bitmap.GetHeight();
	return true;
}
