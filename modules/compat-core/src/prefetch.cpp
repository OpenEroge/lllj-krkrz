//---------------------------------------------------------------------------
// Background image prefetch for game scripts.
//
// CompatNative.prefetchImage(name)
//   Decodes the image on the engine's async loader thread into the graphic
//   cache, under the key a later Layer.loadImages(name) looks up (default
//   mode, no colour key, no resize). The load then hits the cache, or waits for
//   the decode if it is already running; a request still queued is taken back
//   and decoded synchronously instead. Names that are cached, already queued,
//   or have no extension / loader are ignored. Returns nothing.
// CompatNative.imagePrefetchBusy()
//   True while any prefetch (or Bitmap.loadAsync) is queued or decoding, so a
//   lookahead can avoid piling requests onto the single loader thread.
//---------------------------------------------------------------------------
#include "tjsCommHead.h"
#include "GraphicsLoadThread.h"
#include "compat/compat.h"

COMPAT_NATIVE(prefetchImage)
{
	if (numparams < 1 || param[0]->Type() != tvtString) return TJS_E_BADPARAMCOUNT;
	ttstr name(*param[0]);
	try {
		TVPRequestImagePrefetch(name);
	} catch (...) {
		// A lookahead guesses; a name it cannot resolve is simply not prefetched.
	}
	return TJS_S_OK;
}

COMPAT_NATIVE(imagePrefetchBusy)
{
	if (result) *result = tTJSVariant(TVPIsImagePrefetchLoading() ? 1 : 0);
	return TJS_S_OK;
}
