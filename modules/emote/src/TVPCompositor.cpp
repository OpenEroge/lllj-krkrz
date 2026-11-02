#include "KrkrNSPaths.h"
#include "TVPCompositor.h"
#include "EmoteSWRenderBackend.h"
#ifdef COMPAT_EMOTE_GL
#include "EmoteGLRenderBackend.h"
#endif
#include "KrkrNSLog.h"
#include "Platform.h"
#include "SysInitIntf.h"
#include <SDL.h>
#include <cstring>
// Set by SDLApplication.cpp while an engine session is being torn down
// (process exit on the chain-restart path, or the in-process rebuild window).
extern volatile bool krkrsdl2_engine_teardown_active;

namespace krkrsdl3
{
iTVPRenderBackend* TVPGetRenderBackend()
{
#ifdef COMPAT_EMOTE_GL
    static EmoteGLRenderBackend gpu;
#endif
    static EmoteSWRenderBackend cpu;
    static iTVPRenderBackend* selected = nullptr;
    if (!selected)
    {
        // During teardown, E-mote object destructors (~EmotePlayer / ~D3DAdaptor
        // / SeparateLayerAdaptor::clear) call this just to release leftover
        // targets.  Selecting a backend here would create a GL context and run
        // the full self-test WHILE the window / graphics system are being torn
        // down -- on the real device that crashed the exit (the process died
        // between "restarting application" and the chain load, so the console
        // fell back to hbmenu).  Serve the CPU backend without latching: its
        // DestroyTarget ignores unknown pointers, so the destructors no-op.
        if (krkrsdl2_engine_teardown_active)
        {
            KRKRNS_LOG("[emote] teardown active; serving CPU without selection");
            return &cpu;
        }
        // Backend preference (KRKR-ns Phase 2): the isolated-GL backend was
        // verified on the real Switch (2026-09-07 — correct sprites, mesh
        // raster moved off the CPU), and IsAvailable() now self-tests the
        // driver's full-target rasterization so the Nextendo emulator's
        // software GLES (which clamps drawn triangles to a quarter of the
        // FBO) is detected and rejected automatically. Override markers:
        //   sdmc:/switch/KRKR-ns/emote-cpu.txt  force the CPU backend
        //   sdmc:/switch/KRKR-ns/emote-gl.txt   force the GL backend
        bool wantGl = true;
#ifdef __SWITCH__
        if (FILE* f = fopen(KRKRNS_BASE_A "/emote-gl.txt", "rb"))
        {
            fclose(f);
            wantGl = true;
        }
        if (FILE* f = fopen(KRKRNS_BASE_A "/emote-cpu.txt", "rb"))
        {
            fclose(f);
            wantGl = false;
        }
#else
        // PORT: GL on a hardware driver by default; -emote=cpu forces
        // the CPU rasterizer, -emote=gl forces GL even on software GL.
        bool forceGl = false;
        {
            tTJSVariant opt;
            if (TVPGetCommandLine(TJS_W("-emote"), &opt))
            {
                wantGl = ttstr(opt) != TJS_W("cpu");
                forceGl = ttstr(opt) == TJS_W("gl");
            }
        }
#endif
#ifdef COMPAT_EMOTE_GL
        if (wantGl)
        {
            // The GL backend builds its private context against the primary
            // SDL renderer/window.  The FIRST E-mote call of a process can
            // land inside the launcher->game window transition, before that
            // exists; failing there used to latch the CPU backend for the
            // WHOLE process, and on the emulator the CPU path is 2-3x slower
            // on 1080p E-mote content -- the second game's opening animation
            // then read as "very laggy" while the same game booted directly
            // was smooth.  An absent primary renderer is a TEMPORARY
            // condition: serve the CPU backend for this call and leave the
            // selection open so a later call retries GL.  A genuine self-test
            // failure (renderer present, test still fails) latches the CPU
            // backend exactly as before.
            if (!SDL_GL_GetCurrentWindow()) // PORT: no SDL_Renderer in krkrz
            {
                KRKRNS_LOG("[emote] GL not ready (no primary renderer yet); serving CPU, will retry GL");
                return &cpu;
            }
            // PORT: a software GL driver rasterizes slower than the CPU
            // backend's threaded row bands; keep GL for real GPUs only.
            bool softwareGl = false;
            using GetStringFn = const unsigned char* (*)(unsigned int);
            if (auto getString = reinterpret_cast<GetStringFn>(SDL_GL_GetProcAddress("glGetString")))
            {
                const char* name = reinterpret_cast<const char*>(getString(0x1F01 /* GL_RENDERER */));
                softwareGl = name && (std::strstr(name, "llvmpipe") || std::strstr(name, "softpipe") ||
                                      std::strstr(name, "swrast"));
            }
            if (softwareGl && !forceGl)
            {
                selected = &cpu;
            }
            else if (gpu.IsAvailable())
            {
                selected = &gpu;
            }
            else
            {
                selected = &cpu;
                KRKRNS_LOG("[emote] GL self-test failed; CPU backend latched");
            }
        }
        else
#endif
        {
            (void)wantGl;
            selected = &cpu;
        }
#ifdef COMPAT_EMOTE_GL
        TVPConsoleLog("selected %s backend", selected == &gpu ? "isolated GL" : "CPU");
#endif
    }
    return selected;
}

// KRKR-ns: tell the selected backend that the SDL renderer it cached is gone,
// because the host is rebuilding the engine in-process.
//
// The CPU backend normally notices a NEW renderer pointer by itself, but after
// an engine restart the allocator frequently hands the replacement renderer the
// same address -- the comparison then cannot see the change and stale GPU
// handles (targets, textures, scratch) are reused against a renderer that no
// longer exists.  That shows up as a title screen whose UI draws while the
// background and E-mote logo animation stay empty, and as a freeze on a later
// restart.  This reset is unconditional.
void TVPResetRenderBackendForEngineRestart()
{
    if (iTVPRenderBackend* backend = TVPGetRenderBackend())
        backend->ResetForEngineRestart();
}
} // namespace krkrsdl3

