# emote

E-mote animation player (`motionplayer.dll`, the `Motion` class tree) and the
`PSBFile` class, vendored from [krkr-ns](https://github.com/DrFlyingPig/krkr-ns)
with local fixes (particles, loop points, parameterized motions); provenance and
every local change are listed in `src/UPSTREAM.txt` (search the sources for `PORT`).

- Registered as the static plugin `compat_emote`; `motionplayer_nod3d.dll` maps to it.
- Meshes are rasterized on the GPU in a private GL context. Software GL drivers
  or a failed self-test select the CPU rasterizer. `-emote=cpu|gl` forces one.
- `CompatNative.emoteStats()`: time per stage since the previous call.
