// Storage system counters (CompatNative.storageStats()) and, with
// -storagetrace=yes, a log line per scan/rebuild/write/clear event.
#include "compat/compat.h"
#include "EngineHooks.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "tjsDictionary.h"
#include "tjsDebug.h"
#include <atomic>
#include <cstring>

namespace {
std::atomic<tjs_uint64> scans{0}, rebuilds{0}, writes{0}, clears{0}, hits{0};
void StorageEvent(const char *kind, const ttstr &path)
{
	if (!std::strcmp(kind, "scan")) ++scans;
	else if (!std::strcmp(kind, "rebuild")) ++rebuilds;
	else if (!std::strcmp(kind, "write")) ++writes;
	else if (!std::strcmp(kind, "clear")) ++clears;
	else if (!std::strcmp(kind, "listing_hit")) ++hits;
	static const bool trace = compat::OptionEnabled(TJS_W("-storagetrace"));
	if (trace) compat::Log(ttstr(TJS_W("paths: ")) + ttstr(kind) + TJS_W(" ") + path);
}
} // namespace

static compat::RegisterInit CompatStorageStatsInit(compat::OrderModule, "storage_stats", [] {
	TVPAddStorageEventHook(StorageEvent);
	if (compat::OptionEnabled(TJS_W("-storagetrace"))) {
		// prelude/storagetrace.tjs logs Scripts.getTraceString(). Release builds
		// do not create the TJS stack tracer; enable it before any prelude runs
		// so trace push/pop stays balanced (independent of full debug mode).
		struct TraceLifetime {
			TraceLifetime() { TJSAddRefStackTracer(); }
			~TraceLifetime() { TJSReleaseStackTracer(); }
		};
		static TraceLifetime traceLifetime;
	}
});

COMPAT_NATIVE(storageStats)
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
