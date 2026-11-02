# packinone

`PackinOne.dll`, a bundle of older plugins:

- linking it links csvParser, layerExImage, layerExRaster, layerExBTOA,
  saveStruct, scriptsEx, shrinkCopy (vendored) and fstat/proxyfs (built in);
- `Scripts.loadDataPack/saveDataPack/makeDataPackDigest/makeDataPackThumb`:
  tjsDataPack saves. Reads `TJS/4s0` (encrypted, IV = `System.title`) and
  `TJS/ns0`, writes `TJS/ns0`. Windows saves embed the pack in JPEG APP11
  segments (16 KiB each, `FF 00` escaped), which is handled. Format notes in
  `src/datapack.cpp`; `tools/formats/pbd_decode.py` is an independent decoder.
- loose natives games call unguarded: `Layer.fetchImageSize`, `Storages.saveOctet`,
  `System.getKnownFolderPath("Captures")` (XDG pictures directory).
- `compat::HxFilename(leaf, domain)` (`include/compat/packinone.h`): Hxv4
  archive names (BLAKE2s), for game profiles whose unpacked data kept them.
