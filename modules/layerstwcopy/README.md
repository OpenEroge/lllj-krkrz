# layerstwcopy

`layerStwCopy.dll`: `Layer.stitchWrappedCopy(dx, dy, dw, dh, src, sx, sy, sw, sh, arr)`,
reimplemented from the original DLL (row loop 0x10002cb0, wrapped row copy
0x10002c50). Destination row `r` reads source row `(oy + r) mod sh` starting at
column `(ox + arr[(oy + r) mod arr.count]) mod sw`, wrapping horizontally.
Yuzusoft's system wipe transition shears its mask with it.
