# Overlay for the "zlib" port: zlib-ng in zlib-compatible mode.
#
# Same API, ABI and symbol names as zlib, so libpng, freetype and the engine
# build against it unchanged, and the shared FFmpeg/libass libraries that bind
# to the executable's exported zlib symbols keep working. zlib-ng inflates
# faster and has SIMD adler32/crc32, which is most of a PNG background's load
# time. CPU features are detected at run time (WITH_NATIVE_INSTRUCTIONS OFF),
# so the binary still runs on any x86-64.
#
# Based on vcpkg's zlib-ng port (version and SHA512 of the zlib-ng 2.3.3 tag);
# vcpkg-cmake-wrapper.cmake and usage are vcpkg's zlib port's, so
# find_package(ZLIB) and ZLIB::ZLIB resolve as before.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO zlib-ng/zlib-ng
    REF "${VERSION}"
    SHA512 e2057c764f1d5aaee738edee7e977182c5b097e3c95489dcd8de813f237d92a05daaa86d68d44b331d9fec5d1802586a8f6cfb658ba849874aaa14e72a8107f5
    HEAD_REF develop
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_COMPAT=ON
        -DZLIB_ENABLE_TESTS=OFF
        -DZLIBNG_ENABLE_TESTS=OFF
        -DWITH_GTEST=OFF
        -DWITH_NEW_STRATEGIES=ON
        -DWITH_NATIVE_INSTRUCTIONS=OFF
        -DWITH_RUNTIME_CPU_DETECTION=ON
    OPTIONS_RELEASE
        -DWITH_OPTIM=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ZLIB)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share"
                    "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
