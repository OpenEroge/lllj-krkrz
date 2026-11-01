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

set(KRKRZ_SRC_ADD ${KRKRZ_SRC_ADD} ${LLLJ_SRC} PARENT_SCOPE)
set(KRKRZ_INC ${KRKRZ_INC} ${LLLJ_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
