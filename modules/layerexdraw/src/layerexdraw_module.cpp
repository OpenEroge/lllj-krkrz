// PORT: registers the GDI+ emulation as the static plugin "compat_layerexdraw".
//
// The game links layerExDraw.dll; prelude/layerexdraw.tjs maps that name to
// Plugins.link("compat_layerexdraw"), which lands in link() below. The NCB tables
// of LayerExDraw.cpp (GdiPlus, Appearance, Path, the Layer members) are put on
// the auto-register lists of this module by the prefix's TVP_PLUGIN_NAME, so
// AllRegist() below registers only them.
//
// the vendored ncbind (vendor/krkr-plugins/ncbind).cpp is not compiled (see emote/emote_module.cpp).
#include "compat/ncbind.h"
#include "SysInitIntf.h"

namespace {

int32_t STDCALL compat_layerexdraw_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL compat_layerexdraw_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin compat_layerexdraw_plugin = {
	TJS_W("compat_layerexdraw"), compat_layerexdraw_link, compat_layerexdraw_unlink,
};

tTVPAtStart compat_layerexdraw_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&compat_layerexdraw_plugin); });

} // namespace
