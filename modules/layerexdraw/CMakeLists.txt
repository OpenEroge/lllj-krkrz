# layerexdraw: layerExDraw.dll (GDI+ drawing) vendored from krkr-ns
# (src/UPSTREAM.txt), rasterized by PlutoVG (third_party/plutovg, MIT +
# FreeType license, plain C).
file(GLOB _src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB _plutovg CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/plutovg/source/*.c)
# the prefix replaces krkr-ns' precompiled header; shim/ provides TVPSysFont.h,
# which must stay invisible to the rest of the engine
set_property(SOURCE ${_src} DIRECTORY ${KRKRZ_CORE_DIR} APPEND PROPERTY COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim/layerexdraw_prefix.h;-iquote;${CMAKE_CURRENT_SOURCE_DIR}/src/shim")
# PLUTOVG_BUILD_STATIC only changes visibility macros (empty on Linux)
set_property(SOURCE ${_plutovg} DIRECTORY ${KRKRZ_CORE_DIR} APPEND PROPERTY COMPILE_DEFINITIONS PLUTOVG_BUILD_STATIC)
compat_module(layerexdraw
    SOURCES ${_src} ${_plutovg}
    PRELUDE prelude/layerexdraw.tjs
    PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/plutovg/include)
