# webp: WebP data under other image extensions (engine graphic sniffer).
find_package(PkgConfig REQUIRED)
pkg_check_modules(COMPAT_WEBP REQUIRED IMPORTED_TARGET GLOBAL libwebp)
compat_module(webp LIBS PkgConfig::COMPAT_WEBP)
