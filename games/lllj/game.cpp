// ライムライト・レモネードジャム: native side of the game profile.
//   * mounts the Chinese patch (zh-patch.toml, modules/xpck-patch);
//   * storage fallbacks for names the game asks for but its data lacks.
#include "tjsCommHead.h"
#include "EngineHooks.h"
#include "compat/compat.h"
#include "compat/packinone.h"
#include "compat/xpck_patch.h"
#include <stdexcept>

#include "zh_patch_toml.inc" // static const char LLLJ_ZH_PATCH_TOML[] (CMakeLists.txt)

namespace {

void StorageFallback(const ttstr &leaf, std::vector<ttstr> &alternatives)
{
	// The Chinese patch's saves serialize this message-frame image name. Its
	// language-neutral counterpart has the same role in the Japanese assets.
	// Only consulted after the ordinary lookup, so the patch's own file wins.
	if (leaf.AsLowerCase() == TJS_W("option_cn__bg0.tlg")) alternatives.push_back(TJS_W("option__bg0.tlg"));
	// 16 files of the unpacked Hxv4 archives are stored under their hashed
	// names because their real names are unknown.
	alternatives.push_back(compat::HxFilename(leaf, TJS_W("xp3hnp")));
}

void Init()
{
	try {
		compat::xpck::Install(LLLJ_ZH_PATCH_TOML);
	} catch (const std::exception &e) {
		compat::Log(ttstr(TJS_W("lllj: Chinese patch config: ")) + ttstr(e.what()));
	}
	TVPAddStorageFallbackHook(StorageFallback);
}

compat::RegisterInit LLLJInit(compat::OrderGame, "lllj", Init);

} // namespace
