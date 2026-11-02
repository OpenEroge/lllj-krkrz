// compat-core: the registry every compatibility module plugs into.
//
// Modules are plain C++ sources compiled into the engine executable (so they
// may use engine-internal headers). They register three kinds of things with
// static objects, which compat-core runs once the engine's core classes exist
// and the command line is parsed (tTVPAtInstallClass "CompatNative"):
//
//   1. init functions     compat::RegisterInit      e.g. register engine hooks
//   2. TJS natives        COMPAT_NATIVE(name)       -> CompatNative.name(...)
//   3. TJS preludes       modules/<m>/prelude/*.tjs (embedded by CMake)
//
// Inits run first, then the preludes, both ordered by (order, name). Use the
// constants below for the order so a game profile always runs after the
// generic modules it builds on.
#pragma once
#include "tjsCommHead.h"

namespace compat {

enum Order : int {
	OrderCore = 0,     // compat-core itself
	OrderModule = 100, // generic modules
	OrderGame = 1000,  // game profiles
};

// native function callable from TJS as CompatNative.<name>(...)
typedef tjs_error (*NativeFunc)(tTJSVariant *result, tjs_int numparams,
	tTJSVariant **param, iTJSDispatch2 *objthis);

struct RegisterNative {
	RegisterNative(const tjs_char *name, NativeFunc fn);
};
struct RegisterInit {
	RegisterInit(int order, const char *name, void (*fn)());
};
struct RegisterPrelude {
	// text must stay valid for the program's lifetime (embedded literal)
	RegisterPrelude(int order, const char *name, const char16_t *text);
};

// Always-visible log line ("(compat) " + message); game scripts usually
// silence Debug.message in release mode.
void Log(const ttstr &message);
// Command line option -name=value. Returns false when absent.
bool Option(const tjs_char *name, ttstr &value);
// True when -name=yes|1|true.
bool OptionEnabled(const tjs_char *name);

} // namespace compat

#define COMPAT_NATIVE(name) \
	static tjs_error compat_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis); \
	static compat::RegisterNative compat_reg_##name(TJS_W(#name), compat_native_##name); \
	static tjs_error compat_native_##name(tTJSVariant *result, tjs_int numparams, \
		tTJSVariant **param, iTJSDispatch2 *objthis)
