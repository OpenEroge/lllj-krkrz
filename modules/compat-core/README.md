# compat-core

The foundation every other module and game profile builds on.

- **Registry** (`include/compat/compat.h`): modules register TJS natives
  (`COMPAT_NATIVE(name)` → `CompatNative.name(...)`), init functions
  (`compat::RegisterInit`) and TJS preludes (`PRELUDE` in CMake). When the engine
  installs the `CompatNative` class, compat-core runs the inits, creates
  `global.CompatNative`, then runs the preludes, each ordered by
  `(order, name)`: core 0, modules 100, games 1000.
- **DLL mapping** (`prelude/core.tjs`): wraps `Plugins.link` so the Windows DLL
  names games link resolve to a static plugin (`compat_mapDll`) or a TJS shim
  (`compat_shim`). `prelude/krkr-plugins.tjs` maps the vendored krkrz plugins.
  Also removes `kirikiriz_generic`, which would send PC games down console code paths.
- **Stubs** (`compat_stub`, `compat_stubs`): missing members that log
  `(compat) stub: Name` once and return the "absent" value.
- **`CompatNative.attachMembers(src, dst[, overwrite])`**: copies the members of a
  TJS helper class (named `...Members`) onto a native class such as `Layer` or
  `Window`, the way ncbind's `NCB_ATTACH_CLASS` does.
- **`compat/ncbind.h`**: lets ncbind-based code (E-mote, layerExDraw, textrender)
  compile against engine-internal headers inside the executable, with private
  symbol names per module (`TVP_PLUGIN_NAME`).
- **`compat/layer.h`**: `compat::LayerOf()` and `Layer_FetchImageSize()`.

Writing a new module: see [docs/modules.md](../../docs/modules.md).
