# compat-core: registry for natives, init functions and TJS preludes
# (include/compat/compat.h), Plugins.link mapping (prelude/core.tjs), the
# ncbind adapter for modules built into the executable (compat/ncbind.h) and
# shared Layer helpers (compat/layer.h).
compat_module(compat-core
    ORDER 0
    PRELUDE prelude/core.tjs prelude/krkr-plugins.tjs
    # ncbind/ncbind.hpp comes from the vendored krkrz plugins
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include ${KRKRZ_PLUGIN_DIR}
            ${COMPAT_MODULES_DIR}/third_party/tomlplusplus)
