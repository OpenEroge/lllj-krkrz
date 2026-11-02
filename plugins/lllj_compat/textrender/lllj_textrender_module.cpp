// LLLJ: registers krkrsdl3's textrender as the static plugin "lllj_textrender".
//
// The game's TextRender.tjs links textrender.dll and derives TextRender from
// TextRenderBase; prelude.tjs maps that name to Plugins.link("lllj_textrender"),
// which lands in link() below. The prefix's TVP_PLUGIN_NAME gives this module
// its own ncbind auto-register list, so AllRegist() registers only
// TextRenderBase (see layerexdraw/lllj_layerexdraw_module.cpp).
#include "lllj_ncbind.h"
#include "SysInitIntf.h"

namespace {

int32_t STDCALL lllj_textrender_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL lllj_textrender_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin lllj_textrender_plugin = {
	TJS_W("lllj_textrender"), lllj_textrender_link, lllj_textrender_unlink,
};

tTVPAtStart lllj_textrender_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&lllj_textrender_plugin); });

} // namespace
