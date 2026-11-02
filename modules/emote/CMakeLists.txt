# emote: E-mote player (motionplayer.dll) and PSBFile, vendored from krkr-ns
# (src/UPSTREAM.txt). Meshes are rasterized on the GPU in a private GL context
# (COMPAT_EMOTE_GL); the CPU rasterizer remains for software GL and -emote=cpu.
file(GLOB _src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/psb/*.cpp)
# krkr-ns builds these with a precompiled header; shim/emote_prefix.h is
# force-included instead so the sources stay close to upstream.
set_property(SOURCE ${_src} DIRECTORY ${KRKRZ_CORE_DIR}
    APPEND PROPERTY COMPILE_OPTIONS "-include;emote_prefix.h")
compat_module(emote
    SOURCES ${_src}
    PRELUDE prelude/emote.tjs
    PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/src/shim ${CMAKE_CURRENT_SOURCE_DIR}/src
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/psb
    DEFINITIONS COMPAT_EMOTE_GL)
