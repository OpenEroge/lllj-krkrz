// PORT: registers the E-mote runtime as the static plugin "compat_emote".
//
// The game links motionplayer_nod3d.dll; prelude/emote.tjs maps that name to
// Plugins.link("compat_emote"), which lands in link() below. ncbind's
// auto-register list for this module is private (see compat/ncbind.h), so only
// the Motion class tree from emoteplayer.cpp is registered here.
//
// the vendored ncbind (vendor/krkr-plugins/ncbind).cpp is deliberately not compiled: it expects the plugin
// ABI (tp_stub) for TVPInitImportStub and defines V2Link/V2Unlink. The engine
// side needs neither for code that is linked into the executable.
#include "compat/ncbind.h"
#include "SysInitIntf.h"

// TVPCompositor.cpp (krkr-ns) checks this while the engine is shutting down an
// in-process restart; krkrz never does that, so it stays false.
volatile bool krkrsdl2_engine_teardown_active = false;

namespace {

int32_t STDCALL compat_emote_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL compat_emote_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin compat_emote_plugin = {
	TJS_W("compat_emote"), compat_emote_link, compat_emote_unlink,
};

tTVPAtStart compat_emote_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&compat_emote_plugin); });

} // namespace

//---------------------------------------------------------------------------
// CompatNative.emoteStats(): E-mote time per stage since the previous call
#include "KrkrNSProf.h"
#include "compat/compat.h"
#include "tjsDictionary.h"

namespace {
double compatEmoteMs[COMPAT_EMOTE_SLOTS];
tjs_int compatEmoteCount[COMPAT_EMOTE_SLOTS];
}

void CompatEmoteProf(int slot, double ms)
{
	if (slot < 0 || slot >= COMPAT_EMOTE_SLOTS) return;
	compatEmoteMs[slot] += ms;
	++compatEmoteCount[slot];
}

COMPAT_NATIVE(emoteStats)
{
	static const tjs_char *names[COMPAT_EMOTE_SLOTS] = {
		TJS_W("progress"), TJS_W("draw"), TJS_W("readback"), TJS_W("lock"),
		TJS_W("convert"), TJS_W("mesh"), TJS_W("progress_call"), TJS_W("draw_call") };
	iTJSDispatch2 *d = TJSCreateDictionaryObject();
	for (int i = 0; i < COMPAT_EMOTE_SLOTS; ++i) {
		tTJSVariant ms(compatEmoteMs[i]), n(compatEmoteCount[i]);
		d->PropSet(TJS_MEMBERENSURE, (ttstr(names[i]) + TJS_W("_ms")).c_str(), nullptr, &ms, d);
		d->PropSet(TJS_MEMBERENSURE, (ttstr(names[i]) + TJS_W("_n")).c_str(), nullptr, &n, d);
		compatEmoteMs[i] = 0; compatEmoteCount[i] = 0;
	}
	if (result) *result = tTJSVariant(d, d);
	d->Release();
	return TJS_S_OK;
}

//---------------------------------------------------------------------------
// PSBFile: krkr-ns' PSB reader class (psb/), a built-in class like on krkr-ns.
#include "psb/PsbFilePlugin.h"
#include "Extension.h"
static iTJSDispatch2 *CompatCreatePSBFile(iTJSDispatch2 *) { return TVPCreateNativeClass_PSBFile(); }
static tTVPAtInstallClass CompatInstallPSBFile(TJS_W("PSBFile"), CompatCreatePSBFile, TJS_W(""));
