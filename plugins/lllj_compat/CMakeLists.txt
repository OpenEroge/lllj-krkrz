# LLLJ compatibility layer, compiled straight into the engine executable
# (engine-internal headers available). Not a krkrz_plugin() target: the
# sources are handed back to src/core through PARENT_SCOPE.
set(LLLJ_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# embed prelude.tjs as a UTF-16 raw string literal
file(READ ${LLLJ_DIR}/src/prelude.tjs LLLJ_PRELUDE)
set(LLLJ_GEN ${CMAKE_CURRENT_BINARY_DIR}/lllj_prelude.inc)
file(CONFIGURE OUTPUT ${LLLJ_GEN}
     CONTENT "static const char16_t LLLJ_PRELUDE[] = uR\"LLLJ_PRELUDE(@LLLJ_PRELUDE@)LLLJ_PRELUDE\";\n"
     @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LLLJ_DIR}/src/prelude.tjs)

file(GLOB LLLJ_SRC CONFIGURE_DEPENDS ${LLLJ_DIR}/src/*.cpp)

# E-mote runtime vendored from krkr-ns (see emote/UPSTREAM.txt). emote/shim
# adapts its SDL2/Switch/ncbind includes; shim/lllj_ncbind.h is the ncbind
# binding shared with layerexdraw/ (ncbind itself comes from krkrz_dev).
file(GLOB LLLJ_EMOTE_SRC CONFIGURE_DEPENDS ${LLLJ_DIR}/emote/*.cpp)
# krkr-ns builds them with a precompiled header; emote/shim/lllj_emote_prefix.h
# is force-included instead so the sources stay unchanged. They are compiled by
# the engine target, so the property is set in that directory's scope.
set_source_files_properties(${LLLJ_EMOTE_SRC} DIRECTORY ${CMAKE_SOURCE_DIR}/src/core
    PROPERTIES COMPILE_OPTIONS "-include;lllj_emote_prefix.h")
# Mesh rasterization runs in the isolated-GL backend (LLLJ_EMOTE_GL); the CPU
# rasterizer remains as the self-test fallback and for -lllj_emote=cpu.
set_property(SOURCE ${LLLJ_EMOTE_SRC} DIRECTORY ${CMAKE_SOURCE_DIR}/src/core
    APPEND PROPERTY COMPILE_DEFINITIONS LLLJ_EMOTE_GL)

# layerExDraw.dll: GDI+ emulation vendored from krkr-ns (layerexdraw/UPSTREAM.txt),
# rasterized by PlutoVG (third_party/plutovg/UPSTREAM.txt). Like E-mote, the
# module prefix is force-included. layerexdraw/shim is per source (it provides
# TVPSysFont.h) so that name stays invisible to the rest of the engine.
file(GLOB LLLJ_LAYEREXDRAW_SRC CONFIGURE_DEPENDS ${LLLJ_DIR}/layerexdraw/*.cpp)
set_source_files_properties(${LLLJ_LAYEREXDRAW_SRC} DIRECTORY ${CMAKE_SOURCE_DIR}/src/core
    PROPERTIES COMPILE_OPTIONS
    "-include;${LLLJ_DIR}/layerexdraw/shim/lllj_layerexdraw_prefix.h;-iquote;${LLLJ_DIR}/layerexdraw/shim")
# PlutoVG (MIT + FreeType license): plain C, linked statically (PLUTOVG_BUILD_STATIC
# only changes the visibility macros, which are empty on Linux).
file(GLOB LLLJ_PLUTOVG_SRC CONFIGURE_DEPENDS ${LLLJ_DIR}/third_party/plutovg/source/*.c)
set_property(SOURCE ${LLLJ_PLUTOVG_SRC} DIRECTORY ${CMAKE_SOURCE_DIR}/src/core
    APPEND PROPERTY COMPILE_DEFINITIONS PLUTOVG_BUILD_STATIC)

# DSPFilters (MIT, third_party/DSPFilters/UPSTREAM.txt) for WaveSoundBuffer.WaveDSPFilter
# (src/wave_filters.cpp).
file(GLOB LLLJ_DSPFILTERS_SRC CONFIGURE_DEPENDS ${LLLJ_DIR}/third_party/DSPFilters/source/*.cpp)

# Prebuilt LGPL shared FFmpeg (tools/fetch_ffmpeg.sh) for the movie backend
# (src/FFmpegMoviePlayer.cpp). The libraries are copied next to the executable
# by run.sh; $ORIGIN is added as a DT_RPATH (the engine links with
# --disable-new-dtags), which also covers libavcodec's own libswresample.
if(NOT LLLJ_FFMPEG_DIR)
    get_filename_component(LLLJ_FFMPEG_DIR "${LLLJ_DIR}/../../third_party/ffmpeg" ABSOLUTE)
endif()
if(NOT EXISTS "${LLLJ_FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg not found in ${LLLJ_FFMPEG_DIR}; run tools/fetch_ffmpeg.sh")
endif()
set(LLLJ_FFMPEG_LIBS)
foreach(lib avformat avcodec avutil)
    list(APPEND LLLJ_FFMPEG_LIBS "${LLLJ_FFMPEG_DIR}/lib/lib${lib}.so")
endforeach()
set(CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} "\$ORIGIN" PARENT_SCOPE)

set(KRKRZ_SRC_ADD ${KRKRZ_SRC_ADD} ${LLLJ_SRC} ${LLLJ_EMOTE_SRC} ${LLLJ_LAYEREXDRAW_SRC}
    ${LLLJ_PLUTOVG_SRC} ${LLLJ_DSPFILTERS_SRC} PARENT_SCOPE)
set(KRKRZ_LIB_ADD ${KRKRZ_LIB_ADD} ${LLLJ_FFMPEG_LIBS} PARENT_SCOPE)
set(KRKRZ_INC ${KRKRZ_INC} ${LLLJ_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}
    ${LLLJ_DIR}/shim ${LLLJ_DIR}/emote/shim ${LLLJ_DIR}/emote ${CMAKE_SOURCE_DIR}/src/plugins
    ${LLLJ_FFMPEG_DIR}/include ${LLLJ_DIR}/third_party/DSPFilters/include
    ${LLLJ_DIR}/third_party/plutovg/include
    ${CMAKE_SOURCE_DIR}/src/core/external/movie-player/extlibs/libyuv/include PARENT_SCOPE)
