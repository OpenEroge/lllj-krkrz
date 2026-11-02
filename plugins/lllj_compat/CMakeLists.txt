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
# adapts its SDL2/Switch/ncbind includes; ncbind itself comes from krkrz_dev.
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

set(KRKRZ_SRC_ADD ${KRKRZ_SRC_ADD} ${LLLJ_SRC} ${LLLJ_EMOTE_SRC} PARENT_SCOPE)
set(KRKRZ_INC ${KRKRZ_INC} ${LLLJ_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}
    ${LLLJ_DIR}/emote/shim ${LLLJ_DIR}/emote ${CMAKE_SOURCE_DIR}/src/plugins PARENT_SCOPE)
