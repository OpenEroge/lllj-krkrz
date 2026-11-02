# msdfrender

`msdfrender.dll`: the `Layer` members behind MSDF/SDF atlas fonts
(`makeGlyphMSDF`, `makeGlyphSDF`, `operateGlyphToProvince`,
`makeBitmapFromProvince`, `getAscentHeight`), reimplemented from the original
32-bit DLL's disassembly (addresses in `src/msdfrender.cpp`). MSDF coverage is
the median of RGB, SDF the alpha; `cov = 128 + (sd - 128) * param`.
Glyphs are drawn with the engine's `Layer.drawGlyph`.
