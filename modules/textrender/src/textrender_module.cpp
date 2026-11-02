// PORT: registers krkrsdl3's textrender as the static plugin "compat_textrender".
//
// The game's TextRender.tjs links textrender.dll and derives TextRender from
// TextRenderBase; prelude/textrender.tjs maps that name to Plugins.link("compat_textrender"),
// which lands in link() below. The prefix's TVP_PLUGIN_NAME gives this module
// its own ncbind auto-register list, so AllRegist() registers only
// TextRenderBase (see layerexdraw/layerexdraw_module.cpp).
#include "compat/ncbind.h"
#include "SysInitIntf.h"

namespace {

int32_t STDCALL compat_textrender_link(iTVPFunctionExporter *)
{
	ncbAutoRegister::AllRegist();
	return 0; // S_OK
}

int32_t STDCALL compat_textrender_unlink()
{
	ncbAutoRegister::AllUnregist();
	return 0; // S_OK
}

const iTVPStaticPlugin compat_textrender_plugin = {
	TJS_W("compat_textrender"), compat_textrender_link, compat_textrender_unlink,
};

tTVPAtStart compat_textrender_register(TVP_ATSTART_PRI_PREPARE,
	[]() { TVPRegisterPlugin(&compat_textrender_plugin); });

} // namespace
