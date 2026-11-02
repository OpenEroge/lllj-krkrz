#pragma once
// LLLJ: KRKR-ns per-frame profiler hooks, accumulated by lllj_emote_module.cpp
// and read back with LLLJNative.emoteStats() (ms totals and call counts since
// the last read).
enum LLLJEmoteProfSlot {
	LLLJ_EMOTE_PROGRESS, LLLJ_EMOTE_DRAW, LLLJ_EMOTE_READBACK, LLLJ_EMOTE_LOCK,
	LLLJ_EMOTE_CONVERT, LLLJ_EMOTE_MESH, LLLJ_EMOTE_PROG_CALL, LLLJ_EMOTE_DRAW_CALL,
	LLLJ_EMOTE_SLOTS
};
void LLLJEmoteProf(int slot, double ms);
static inline void krkrsdl2_prof_emote_progress(double ms) { LLLJEmoteProf(LLLJ_EMOTE_PROGRESS, ms); }
static inline void krkrsdl2_prof_emote_draw(double ms) { LLLJEmoteProf(LLLJ_EMOTE_DRAW, ms); }
static inline void krkrsdl2_prof_emote_readback(double ms) { LLLJEmoteProf(LLLJ_EMOTE_READBACK, ms); }
static inline void krkrsdl2_prof_emote_lock(double ms) { LLLJEmoteProf(LLLJ_EMOTE_LOCK, ms); }
static inline void krkrsdl2_prof_emote_convert(double ms) { LLLJEmoteProf(LLLJ_EMOTE_CONVERT, ms); }
static inline void krkrsdl2_prof_emote_mesh() { LLLJEmoteProf(LLLJ_EMOTE_MESH, 0); }
static inline void krkrsdl2_prof_emote_prog_call() { LLLJEmoteProf(LLLJ_EMOTE_PROG_CALL, 0); }
static inline void krkrsdl2_prof_emote_draw_call() { LLLJEmoteProf(LLLJ_EMOTE_DRAW_CALL, 0); }
