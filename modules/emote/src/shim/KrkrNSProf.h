#pragma once
// PORT: KRKR-ns per-frame profiler hooks, accumulated by emote_module.cpp
// and read back with CompatNative.emoteStats() (ms totals and call counts since
// the last read).
enum CompatEmoteProfSlot {
	COMPAT_EMOTE_PROGRESS, COMPAT_EMOTE_DRAW, COMPAT_EMOTE_READBACK, COMPAT_EMOTE_LOCK,
	COMPAT_EMOTE_CONVERT, COMPAT_EMOTE_MESH, COMPAT_EMOTE_PROG_CALL, COMPAT_EMOTE_DRAW_CALL,
	COMPAT_EMOTE_SLOTS
};
void CompatEmoteProf(int slot, double ms);
static inline void krkrsdl2_prof_emote_progress(double ms) { CompatEmoteProf(COMPAT_EMOTE_PROGRESS, ms); }
static inline void krkrsdl2_prof_emote_draw(double ms) { CompatEmoteProf(COMPAT_EMOTE_DRAW, ms); }
static inline void krkrsdl2_prof_emote_readback(double ms) { CompatEmoteProf(COMPAT_EMOTE_READBACK, ms); }
static inline void krkrsdl2_prof_emote_lock(double ms) { CompatEmoteProf(COMPAT_EMOTE_LOCK, ms); }
static inline void krkrsdl2_prof_emote_convert(double ms) { CompatEmoteProf(COMPAT_EMOTE_CONVERT, ms); }
static inline void krkrsdl2_prof_emote_mesh() { CompatEmoteProf(COMPAT_EMOTE_MESH, 0); }
static inline void krkrsdl2_prof_emote_prog_call() { CompatEmoteProf(COMPAT_EMOTE_PROG_CALL, 0); }
static inline void krkrsdl2_prof_emote_draw_call() { CompatEmoteProf(COMPAT_EMOTE_DRAW_CALL, 0); }
