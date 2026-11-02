// LLLJ: registers the GDI+ emulation as the static plugin "lllj_layerexdraw".
//
// The game links layerExDraw.dll; prelude.tjs maps that name to
// Plugins.link("lllj_layerexdraw"), which lands in link() below. The NCB tables
// of LayerExDraw.cpp (GdiPlus, Appearance, Path, the Layer members) are put on
// the auto-register lists of this module by the prefix's TVP_PLUGIN_NAME, so
// AllRegist() below registers only them.
//
// krkrz_dev's ncbind.cpp is not compiled (see emote/lllj_emote_module.cpp).
#include "lllj_ncbind.h"
#include "SysInitIntf.h"

namespace {

int32_t STDCALL lllj_layerexdraw_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL lllj_layerexdraw_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin lllj_layerexdraw_plugin = {
	TJS_W("lllj_layerexdraw"), lllj_layerexdraw_link, lllj_layerexdraw_unlink,
};

tTVPAtStart lllj_layerexdraw_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&lllj_layerexdraw_plugin); });

} // namespace
