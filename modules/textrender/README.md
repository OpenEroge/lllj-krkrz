# textrender

`textrender.dll` (`TextRenderBase`: layout, kinsoku, ruby, links, per-character
delays and key waits), vendored from [krkrsdl3](https://github.com/krkrsdl3/krkrsdl3).
Registered as the static plugin `compat_textrender`. krkrsdl3 uses UTF-8
`ttstr`; the force-included `src/shim/textrender_prefix.h` bridges to krkrz's
UTF-16 at every crossing (marked `PORT`). Character widths come from the
script callbacks `onFontChange` / `onGetTextWidth`, as with the original DLL.

License note: krkrsdl3 requires publishing modifications when a port of a
commercial game is distributed (`src/LICENSE.krkrsdl3`).
