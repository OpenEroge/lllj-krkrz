# xpck-patch: translation patches shipped as an XPCK pack plus a hook DLL
# config (include/compat/xpck_patch.h). Needs movie-ffmpeg for subtitles.
find_package(PkgConfig REQUIRED)
pkg_check_modules(COMPAT_ZSTD REQUIRED IMPORTED_TARGET GLOBAL libzstd)
compat_module(xpck-patch
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
    LIBS PkgConfig::COMPAT_ZSTD)
