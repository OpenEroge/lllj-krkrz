// LLLJ compat layer: shared declarations
#pragma once
#include "tjsCommHead.h"
#include <vector>

// native function callable from TJS as LLLJNative.<name>(...)
typedef tjs_error (*LLLJNativeFunc)(tTJSVariant *result, tjs_int numparams,
	tTJSVariant **param, iTJSDispatch2 *objthis);

struct LLLJNativeMethod {
	const tjs_char *name;
	LLLJNativeFunc fn;
};

// static registration helper: static LLLJRegisterMethod r(TJS_W("foo"), fooImpl);
struct LLLJRegisterMethod {
	LLLJRegisterMethod(const tjs_char *name, LLLJNativeFunc fn);
};

// Main-thread hooks from the SDL presentation and dispatch paths.
bool LLLJFpsEnabled();
void LLLJPresented(tjs_uint64 wait_ns, tjs_uint64 show_ns);
void LLLJPollFrameStats();
ttstr LLLJHxFilename(const ttstr &leaf);
void LLLJStorageEvent(const char *kind, const ttstr &path);
bool LLLJIncrementalPaths();
ttstr LLLJSaveAssetFallback(const ttstr &leaf);
// Chinese patch (src/xpck.cpp). LLLJOverlayLookup: 1 = out is the pack path,
// 2 = out is the redirected name to look up instead, 0 = unchanged.
void LLLJZhInit(); // after the command line is parsed: mount unless -lllj_zh=no
bool LLLJZhEnabled();
ttstr LLLJZhRedirect(const ttstr &leaf);
int LLLJOverlayLookup(const ttstr &name, ttstr &out);
ttstr LLLJOverlayLeaf(const ttstr &leaf); // pack path of leaf, or empty
bool LLLJZhReadFile(const ttstr &leaf, std::vector<unsigned char> &data);

// WebP under .png names (src/webp.cpp); false when the stream is not WebP
#include "GraphicsLoaderIntf.h"
bool LLLJLoadWebP(tTVPGraphicSizeCallback sizecallback, tTVPGraphicScanLineCallback scanlinecallback,
	void *callbackdata, iTJSBinaryStream *src, tTVPGraphicLoadMode mode);
bool LLLJLoadHeaderWebP(iTJSBinaryStream *src, iTJSDispatch2 **dic);

#define LLLJ_NATIVE(name) \
	static tjs_error lllj_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis); \
	static LLLJRegisterMethod lllj_reg_##name(TJS_W(#name), lllj_native_##name); \
	static tjs_error lllj_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis)
