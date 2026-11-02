# textrender: textrender.dll (TextRenderBase: layout, per-character delays,
# key waits, links, ruby), vendored from krkrsdl3 (src/UPSTREAM.txt). The
# prefix names the ncbind module and bridges krkrsdl3's UTF-8 ttstr to UTF-16.
file(GLOB _src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set_property(SOURCE ${_src} DIRECTORY ${KRKRZ_CORE_DIR} APPEND PROPERTY COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim/textrender_prefix.h")
compat_module(textrender SOURCES ${_src} PRELUDE prelude/textrender.tjs)
