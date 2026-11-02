// CPU wall-time statistics. A frame is counted only after the draw device swaps;
// neither script timers nor texture uploads are used as a proxy for presentation.
#include "lllj_compat.h"
#include "ThreadIntf.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "CharacterSet.h"
#include "tjsDictionary.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {
using Clock = std::chrono::steady_clock;
tjs_uint64 Now() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
struct Sample {
	tjs_uint64 start = 0, frames = 0, maxGap = 0, wait = 0, show = 0, maxShow = 0;
	TVPRenderStatsSnapshot render{};
};
Sample total, window;
tjs_uint64 lastPresent = 0;
ttstr scene = TJS_W("startup");
void Reset(Sample &s, tjs_uint64 now) {
	s = Sample{};
	s.start = now;
	TVPGetRenderStats(s.render);
}
void Init() {
	if (!total.start) { const auto now = Now(); Reset(total, now); Reset(window, now); }
}
void Set(iTJSDispatch2 *d, const char16_t *name, double v) {
	tTJSVariant value(v);
	d->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, d);
}
iTJSDispatch2 *Snapshot(const Sample &s) {
	const auto now = Now();
	const double elapsed = (now - s.start) / 1e6;
	TVPRenderStatsSnapshot r{}; TVPGetRenderStats(r);
	auto *d = TJSCreateDictionaryObject();
	tTJSVariant label(scene);
	d->PropSet(TJS_MEMBERENSURE, TJS_W("scene"), nullptr, &label, d);
	Set(d, TJS_W("elapsed_ms"), elapsed);
	Set(d, TJS_W("frames"), s.frames);
	Set(d, TJS_W("fps"), elapsed > 0 ? s.frames * 1000.0 / elapsed : 0);
	Set(d, TJS_W("max_interval_ms"), s.maxGap / 1e6);
	Set(d, TJS_W("max_show_ms"), s.maxShow / 1e6);
	Set(d, TJS_W("show_ms"), s.show / 1e6);
	Set(d, TJS_W("wait_ms"), s.wait / 1e6);
	Set(d, TJS_W("compose_ms"), (r.layer_complete_window_ns - s.render.layer_complete_window_ns) / 1e6);
	Set(d, TJS_W("upload_ms"), (r.tex_update_ns - s.render.tex_update_ns + r.tex_render_ns - s.render.tex_render_ns) / 1e6);
	Set(d, TJS_W("since_present_ms"), (now - (lastPresent ? lastPresent : s.start)) / 1e6);
#ifdef KRKRZ_DRAW_STATS
	Set(d, TJS_W("detailed"), 1);
#else
	Set(d, TJS_W("detailed"), 0);
#endif
	return d;
}
std::string Escape(const ttstr &s) {
	std::string utf8; TVPUtf16ToUtf8(utf8, s.c_str());
	std::ostringstream out;
	for (unsigned char c : utf8) {
		if (c == '"' || c == '\\') out << '\\' << c;
		else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
		else out << c;
	}
	return out.str();
}
}

bool LLLJFpsEnabled() {
	static const bool enabled = [] {
		tTJSVariant v;
		return TVPGetCommandLine(TJS_W("-lllj_fps"), &v) &&
			(ttstr(v) == TJS_W("yes") || ttstr(v) == TJS_W("1") || ttstr(v) == TJS_W("true"));
	}();
	return enabled;
}
void LLLJPresented(tjs_uint64 wait, tjs_uint64 show) {
	Init();
	const auto now = Now();
	for (auto *s : {&total, &window}) {
		s->frames++;
		if (lastPresent) s->maxGap = std::max(s->maxGap, now - lastPresent);
		s->wait += wait; s->show += show; s->maxShow = std::max(s->maxShow, show);
	}
	lastPresent = now;
}
void LLLJPollFrameStats() {
	if (!LLLJFpsEnabled()) return;
	Init();
	const auto now = Now();
	if (now - window.start < 1000000000ULL) return;
	auto *d = Snapshot(window);
	std::ostringstream out;
	out << "(lllj) fps: {\"scene\":\"" << Escape(scene) << '"';
	for (const auto *key : {u"elapsed_ms", u"frames", u"fps", u"max_interval_ms", u"max_show_ms",
		u"show_ms", u"wait_ms", u"compose_ms", u"upload_ms", u"since_present_ms", u"detailed"}) {
		tTJSVariant value; d->PropGet(0, key, nullptr, &value, d);
		std::string name; TVPUtf16ToUtf8(name, key);
		out << ",\"" << name << "\":" << std::fixed << std::setprecision(3) << double(value);
	}
	out << '}'; d->Release();
	TVPAddImportantLog(ttstr(out.str().c_str()));
	Reset(window, now);
}
LLLJ_NATIVE(frameStats) {
	Init(); auto *d = Snapshot(total);
	if (result) *result = tTJSVariant(d, d);
	d->Release(); return TJS_S_OK;
}
LLLJ_NATIVE(resetFrameStats) {
	if (numparams) scene = ttstr(*param[0]);
	const auto now = Now(); Reset(total, now); Reset(window, now); lastPresent = 0;
	if (result) *result = (tjs_int)1;
	return TJS_S_OK;
}
