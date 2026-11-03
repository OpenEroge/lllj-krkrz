# Vendored krkrz plugins

Copied verbatim from the submodules of
[wamsoft/krkrz_dev](https://github.com/wamsoft/krkrz_dev) at 21d20e9745e13c9ffe68ae2eb012548afe73aaa8.
Each directory keeps its own license file. Only the plugins the build links are kept.

| Plugin | Upstream | Commit |
|---|---|---|
| KAGParserEx | https://github.com/wamsoft/KAGParserEx.git | 61d5f1e65f50 |
| csvParser | https://github.com/wamsoft/csvParser.git | 7c757cf4c98e |
| getSample | https://github.com/wamsoft/getSample.git | 6ffb3ecf3637 |
| json | https://github.com/wamsoft/json.git | 331577f704d1 |
| layerExBTOA | https://github.com/wamsoft/layerExBTOA.git | a57e7a25a9e0 |
| layerExImage | https://github.com/wamsoft/layerExImage.git | 3aff625dba5c |
| layerExRaster | https://github.com/wamsoft/layerExRaster.git | a4c9be0c7510 |
| psdfile | https://github.com/wamsoft/psdfile.git | 86d5d05c1e48 |
| saveStruct | https://github.com/wamsoft/saveStruct.git | f03a81a46018 |
| scriptsEx | https://github.com/wamsoft/scriptsEx.git | cc219b5b889b |
| shrinkCopy | https://github.com/wamsoft/shrinkCopy.git | 007fbba3b353 |
| extrans | https://github.com/wamsoft/extrans.git | bd0611eced7e |
| extNagano | https://github.com/wamsoft/extNagano.git | 4b403e2f6847 |
| AlphaMovie | https://github.com/wamsoft/AlphaMovie.git | 0a910880af86 |
| layerExSave | https://github.com/wamsoft/layerExSave.git | b0939dc916f1 |
| ncbind | https://github.com/wamsoft/ncbind.git | 256502b817e6 |
| tp_stub | https://github.com/wamsoft/tp_stub.git | 35ef31ad0630 |
| simplebinder | https://github.com/wamsoft/simplebinder.git | 46a90eac839d |

## Local changes

- `json/Main.cpp`, `csvParser/main.cpp`, `saveStruct/Main.cpp`: `IFileStorage` (json, csvParser) and
  `DictMemberDispCaller` (json, saveStruct) moved into anonymous namespaces. Each plugin defines a different
  class under the same name; linked into one executable, the linker kept a single copy of their inline
  members for both (an ODR violation that LTO reports as `-Wlto-type-mismatch`).
