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

#define LLLJ_NATIVE(name) \
	static tjs_error lllj_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis); \
	static LLLJRegisterMethod lllj_reg_##name(TJS_W(#name), lllj_native_##name); \
	static tjs_error lllj_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis)
