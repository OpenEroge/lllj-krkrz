#include "lllj_compat.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "tjsDictionary.h"
#include <atomic>
#include <cstring>

namespace {
std::atomic<tjs_uint64> scans{0}, rebuilds{0}, writes{0}, clears{0}, hits{0};
}
ttstr LLLJSaveAssetFallback(const ttstr &leaf)
{
	// The localization save serializes this message-frame image name. Its
	// language-neutral counterpart has the same role in the Japanese assets.
	// Called only after ordinary lookup, so a future Chinese patch wins.
	if (leaf.AsLowerCase() == TJS_W("option_cn__bg0.tlg")) return TJS_W("option__bg0.tlg");
	return ttstr();
}
bool LLLJIncrementalPaths()
{
	// A measurement control: reproduce the original invalidation/rebuild cost
	// with the same executable, resources and rendering instrumentation.
	static const bool enabled = [] {
		tTJSVariant option;
		return !TVPGetCommandLine(TJS_W("-lllj_index"), &option) || ttstr(option) != TJS_W("no");
	}();
	return enabled;
}
void LLLJStorageEvent(const char *kind, const ttstr &path)
{
	if (!std::strcmp(kind, "scan")) ++scans;
	else if (!std::strcmp(kind, "rebuild")) ++rebuilds;
	else if (!std::strcmp(kind, "write")) ++writes;
	else if (!std::strcmp(kind, "clear")) ++clears;
	else if (!std::strcmp(kind, "listing_hit")) ++hits;
	tTJSVariant option;
	if (TVPGetCommandLine(TJS_W("-lllj_paths"), &option) && ttstr(option) == TJS_W("yes"))
		TVPAddImportantLog(ttstr(TJS_W("(lllj) paths: ")) + ttstr(kind) + TJS_W(" ") + path);
}
LLLJ_NATIVE(storageStats)
{
	auto *d = TJSCreateDictionaryObject();
	const char16_t *keys[] = {u"scans", u"rebuilds", u"writes", u"clears", u"listing_hits"};
	const tjs_uint64 values[] = {scans.load(), rebuilds.load(), writes.load(), clears.load(), hits.load()};
	for (int i = 0; i < 5; ++i) {
		tTJSVariant v((tjs_int64)values[i]); d->PropSet(TJS_MEMBERENSURE, keys[i], nullptr, &v, d);
	}
	if (result) *result = tTJSVariant(d, d);
	d->Release(); return TJS_S_OK;
}
