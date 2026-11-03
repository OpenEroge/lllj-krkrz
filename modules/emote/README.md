# emote

E-mote animation player (`motionplayer.dll`, the `Motion` class tree) and the
`PSBFile` class, vendored from [krkr-ns](https://github.com/DrFlyingPig/krkr-ns)
with local fixes (particles, loop points, parameterized motions); provenance and
every local change are listed in `src/UPSTREAM.txt` (search the sources for `PORT`).

- Registered as the static plugin `compat_emote`; `motionplayer_nod3d.dll` maps to it.
- Meshes are rasterized on the GPU in a private GL context. Software GL drivers
  or a failed self-test select the CPU rasterizer. `-emote=cpu|gl` forces one.
- `Motion.Player` has no `useD3D`, like the real `motionplayer_nod3d.dll`, so game scripts
  (`AffineSourceMotion`) draw each motion into its own `SeparateLayerAdaptor` layer rather than
  through one shared full-screen `D3DAdaptor` that is copied to a work layer every frame.
- GL readback (`EmoteGLRenderBackend.cpp`, layer copy in `emoteplayerclass.cpp`):
  - render targets hold BGRA, the Kirikiri layer byte order: the fragment shader swaps R/B on
    write (`swapRB`), so the layer copy is a compare-and-copy (SSE2 on x86) with no swizzle;
  - only the rectangle drawn since the last readback is read, a clear only zeroes what was drawn
    before it, and an adaptor's layer copy visits only the changed rectangle (`EmoteLayerSync`);
  - with OpenGL ES 3 the readback of an adaptor's frame goes through a pixel pack buffer and a
    fence; a dispatch hook copies it into the layer at the start of the next main loop
    iteration, so E-mote layers show one iteration late and the main thread does not wait
    for the GPU. `-emote_readback=sync` reads synchronously. Plain layers and
    `motionWorkLayer` (scripts copy it right after `draw()`) are always synchronous.
- `CompatNative.emoteStats()`: time per stage since the previous call.
