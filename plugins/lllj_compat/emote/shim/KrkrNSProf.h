#pragma once
// LLLJ: KRKR-ns per-frame profiler hooks are not wired up; keep them as no-ops.
static inline void krkrsdl2_prof_emote_progress(double) {}
static inline void krkrsdl2_prof_emote_draw(double) {}
static inline void krkrsdl2_prof_emote_readback(double) {}
static inline void krkrsdl2_prof_emote_lock(double) {}
static inline void krkrsdl2_prof_emote_convert(double) {}
static inline void krkrsdl2_prof_emote_mesh() {}
static inline void krkrsdl2_prof_emote_prog_call() {}
static inline void krkrsdl2_prof_emote_draw_call() {}
