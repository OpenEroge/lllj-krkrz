// LLLJ: registers the E-mote runtime as the static plugin "lllj_emote".
//
// The game links motionplayer_nod3d.dll; prelude.tjs maps that name to
// Plugins.link("lllj_emote"), which lands in link() below. ncbind's
// auto-register list for this module is private (see lllj_ncbind.h), so only
// the Motion class tree from emoteplayer.cpp is registered here.
//
// krkrz_dev's ncbind.cpp is deliberately not compiled: it expects the plugin
// ABI (tp_stub) for TVPInitImportStub and defines V2Link/V2Unlink. The engine
// side needs neither for code that is linked into the executable.
#include "lllj_ncbind.h"
#include "SysInitIntf.h"

// TVPCompositor.cpp (krkr-ns) checks this while the engine is shutting down an
// in-process restart; krkrz never does that, so it stays false.
volatile bool krkrsdl2_engine_teardown_active = false;

namespace {

int32_t STDCALL lllj_emote_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL lllj_emote_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin lllj_emote_plugin = {
	TJS_W("lllj_emote"), lllj_emote_link, lllj_emote_unlink,
};

tTVPAtStart lllj_emote_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&lllj_emote_plugin); });

} // namespace
