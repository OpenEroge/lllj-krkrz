# Top-level build: one executable made of
#   engine/                the krkrz engine (hard fork, git submodule)
#   vendor/krkr-plugins/   the krkrz plugins games link by DLL name
#   modules/               game-independent compatibility modules
#   games/<GAME>/          the game profile: picks modules, adds game glue
# Replaces the krkrz_dev umbrella project; only what this build uses was kept.
cmake_minimum_required(VERSION 3.20)

set(KRKRZ_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(KRKRZ_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/krkr-plugins)
set(TPSTUB_DIR ${KRKRZ_PLUGIN_DIR}/tp_stub)
set(NCBIND_DIR ${KRKRZ_PLUGIN_DIR}/ncbind)
set(SIMPLEBIND_DIR ${KRKRZ_PLUGIN_DIR}/simplebinder)

set(GAME lllj CACHE STRING "game profile to build (a directory under games/)")

# Plugins are looked up in these folders, first match wins. The game profile is
# listed like a static plugin: the engine add_subdirectory()s it and reads
# KRKRZ_SRC_ADD / KRKRZ_INC / KRKRZ_LIB_ADD back (modules/compat.cmake). It
# creates no target, so the engine registers no plugin entry point for it.
set(TVP_PLUGIN_FOLDERS ${KRKRZ_PLUGIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/games)
# All plugins are linked statically into the executable.
set(TVP_PLUGINS KAGParserEx csvParser getSample json layerExBTOA layerExImage
    layerExRaster psdfile saveStruct scriptsEx shrinkCopy extrans extNagano
    AlphaMovie layerExSave ${GAME})
set(TVP_PLUGINS_STATIC ${TVP_PLUGINS})

# Engine options this build always uses.
set(KRKRZ_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(KRKRZ_USE_ELEMENTS OFF CACHE BOOL "" FORCE)
set(KRKRZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
option(KRKRZ_DRAW_STATS "collect DrawThreadPool usage stats (atomic counters)" ON)

# The engine's memory instrumentation is on by default, but it is a dev tool:
# with alloc stats every new/delete/TJS_malloc goes through one pool lock plus
# a header check and atomic counters (about 12% of main-thread samples while
# opening the settings screen), and the overlay keeps a sampler thread alive.
# LLLJ_DEV_STATS=ON builds them back in. FORCE so existing build directories,
# which cached the engine defaults, follow this switch.
# Kept regardless: KRKRZ_ENABLE_ALLOCATOR_STATS (without it the file pool's
# used() is SIZE_MAX, which disables the storage cache's back-pressure check),
# KRKRZ_DRAW_STATS (the tests assert it) and the REPL (the tests drive it).
option(LLLJ_DEV_STATS "build with memory instrumentation (alloc stats, overlay, periodic dump)" OFF)
foreach(_stat KRKRZ_ENABLE_ALLOC_STATS KRKRZ_ENABLE_MEMORY_OVERLAY KRKRZ_ENABLE_PERIODIC_DUMP)
    set(${_stat} ${LLLJ_DEV_STATS} CACHE BOOL "set from LLLJ_DEV_STATS" FORCE)
endforeach()

# vcpkg manifest: the engine's vcpkg.json merged with those of movie-player and
# the plugins. Must run before project(), which starts the manifest install.
include(${KRKRZ_CORE_DIR}/cmake/MergeVcpkgManifest.cmake)
set(_vcpkg_subdirs ${KRKRZ_CORE_DIR}/external/movie-player)
foreach(_plugin IN LISTS TVP_PLUGINS)
    foreach(_folder IN LISTS TVP_PLUGIN_FOLDERS)
        if(EXISTS "${_folder}/${_plugin}")
            list(APPEND _vcpkg_subdirs "${_folder}/${_plugin}")
            break()
        endif()
    endforeach()
endforeach()
krkrz_merge_vcpkg_manifest(
    BASE    "${KRKRZ_CORE_DIR}/vcpkg.json"
    OUTPUT  "${CMAKE_BINARY_DIR}/vcpkg.json"
    SUBDIRS ${_vcpkg_subdirs}
    EXCLUDE "movie-player:glew" "movie-player:glfw3")  # movie-player samples only
set(VCPKG_MANIFEST_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "merged vcpkg manifest" FORCE)
# Overlay ports: "zlib" is zlib-ng in zlib-compatible mode (faster inflate and
# SIMD checksums for PNG, XP3, PSB and freetype; see its portfile).
set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-overlays" CACHE STRING "vcpkg overlay ports" FORCE)

project(lllj_krkrz VERSION 1.0)

# Link-time optimization of everything built in this tree (engine, modules,
# plugins, FetchContent dependencies) in the Release configuration. vcpkg's
# static libraries and FFmpeg are linked as before. CMP0069 NEW makes the
# subprojects with old cmake_minimum_required (ogg, vorbis) honour it too.
option(LLLJ_LTO "link-time optimization (Release)" ON)
if(LLLJ_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _lllj_ipo OUTPUT _lllj_ipo_msg LANGUAGES C CXX)
    if(_lllj_ipo)
        set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(WARNING "LLLJ_LTO: not supported by this toolchain: ${_lllj_ipo_msg}")
    endif()
endif()
# Profile-guided optimization, driven by tools/pgo.sh. "generate" builds an
# instrumented binary that adds its counters to LLLJ_PGO_DIR when it exits;
# "use" optimizes with them. -fprofile-prefix-path makes the profile names
# relative to the build directory, so the profile of the training build
# (build/linux-x64-pgo-gen) applies to the Release build (build/linux-x64).
# A stale profile only costs optimization: functions whose control flow
# changed since the training run are compiled as without PGO. Coverage
# mismatch warnings are off: sources that FetchContent unpacks into the build
# directory (harfbuzz, SDL3, ogg, vorbis...) have different paths in the two
# builds, which GCC reports for every function although it still applies
# their counts.
set(LLLJ_PGO "" CACHE STRING "profile-guided optimization: empty, generate or use (tools/pgo.sh)")
set_property(CACHE LLLJ_PGO PROPERTY STRINGS "" generate use)
set(LLLJ_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/pgo-profile" CACHE PATH "PGO profile directory")
if(LLLJ_PGO STREQUAL "generate")
    set(_lllj_pgo_compile -fprofile-generate=${LLLJ_PGO_DIR} -fprofile-update=prefer-atomic
        -fprofile-prefix-path=${CMAKE_BINARY_DIR})
    set(_lllj_pgo_link -fprofile-generate=${LLLJ_PGO_DIR} -fprofile-update=prefer-atomic)
elseif(LLLJ_PGO STREQUAL "use")
    set(_lllj_pgo_compile -fprofile-use=${LLLJ_PGO_DIR} -fprofile-partial-training
        -fprofile-prefix-path=${CMAKE_BINARY_DIR} -Wno-missing-profile -Wno-coverage-mismatch)
    set(_lllj_pgo_link -fprofile-use=${LLLJ_PGO_DIR} -fprofile-partial-training)
elseif(NOT LLLJ_PGO STREQUAL "")
    message(FATAL_ERROR "LLLJ_PGO must be empty, generate or use (got '${LLLJ_PGO}')")
endif()
foreach(_flag IN LISTS _lllj_pgo_compile)
    add_compile_options($<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:${_flag}>)
endforeach()
foreach(_flag IN LISTS _lllj_pgo_link)
    add_link_options($<$<CONFIG:Release>:${_flag}>)
endforeach()
if(LLLJ_PGO)
    message(STATUS "LLLJ_PGO=${LLLJ_PGO}, profile directory ${LLLJ_PGO_DIR}")
endif()

# vcpkg's toolchain compiles with -fPIC; the executable is not a shared
# library, so nothing interposes its symbols and calls between them may inline.
add_compile_options($<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:-fno-semantic-interposition>)
# Kirikiri code passes typed pointers through casts everywhere, e.g.
# NativeInstanceSupport(..., (iTJSNativeInstance**)&layer). Under strict
# aliasing that write cannot change `layer`; once LTO+PGO turned the virtual
# call into a direct one, IPA mod/ref analysis used exactly that and Layer's
# constructor ignored its parent (every child layer became a primary layer).
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-fno-strict-aliasing>)
# Do not export symbols of the static archives (HarfBuzz, FreeType, zlib, PNG...).
# libass.so references FT_*/hb_*, so ld would export our bundled definitions from
# the non-PIE executable into .dynsym for it; they then interpose the system
# libharfbuzz/libfreetype used by libdecor-cairo's Pango in the same process (the
# Wayland window decorations), and the ABI mix crashes when the window is shown.
add_link_options("LINKER:--exclude-libs,ALL")

# HarfBuzz for glyphware, shared by every user (cmake/CommonExternals.cmake).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CommonExternals.cmake)

add_subdirectory(engine ${CMAKE_CURRENT_BINARY_DIR}/core)
