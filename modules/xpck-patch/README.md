# xpck-patch

Translation patches distributed as an XPCK pack plus a Detours hook DLL whose
behaviour comes from an embedded config (e.g. `lllj_zh.dll`). The game profile
passes that config, written as TOML, to `compat::xpck::Install()`
(`include/compat/xpck_patch.h`); see `games/lllj/zh-patch.toml`.

- The pack is mounted as `xpck://` and wins over every auto path.
- Redirect table: bare names map to per-language names; the language is a TJS
  expression evaluated on the main thread at each lookup, so switching language
  in game takes effect on the next open.
- Subtitles and their fonts are provided to movie-ffmpeg.
- `CompatNative.xpckEnabled()`, `CompatNative.xpckRedirect(name)`.
- `-<option>=no` disables the patch, `-<option>=<path>` selects the pack.

Format: `tools/formats/xpck_unpack.py` (lists/extracts packs, documents the layout).
