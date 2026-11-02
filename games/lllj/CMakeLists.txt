# ライムライト・レモネードジャム (Yuzusoft, 2024) game profile.
#
# The engine adds this directory like a static plugin (top-level CMakeLists.txt,
# GAME=lllj). It picks the compatibility modules the game needs, adds the
# game's own glue, and exports everything to the engine build.
include(${CMAKE_SOURCE_DIR}/modules/compat.cmake)

compat_use(
    compat-core   # registry, Plugins.link mapping
    win32         # Windows API / DLL stand-ins
    packinone     # PackinOne.dll: saves, loose natives, Hxv4 names
    toml          # toml.dll
    emote         # motionplayer.dll, PSBFile
    layerexdraw   # layerExDraw.dll (flowchart, colour picker)
    textrender    # textrender.dll (message text)
    msdfrender    # msdfrender.dll (atlas fonts)
    layerstwcopy  # layerStwCopy.dll (system wipe transition)
    audio         # voice effects, multi-track BGM
    movie-ffmpeg  # OP/ED/MV movies, subtitles
    webp          # WebP backgrounds in the Chinese patch
    xpck-patch    # the Chinese patch pack (lllj_zh.pck)
    diagnostics   # -fpsstats, -storagetrace (used by the tests)
)

compat_embed_text(${CMAKE_CURRENT_SOURCE_DIR}/zh-patch.toml LLLJ_ZH_PATCH_TOML
    ${CMAKE_CURRENT_BINARY_DIR}/zh_patch_toml.inc)
compat_module(lllj
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/game.cpp
    PRELUDE prelude/game.tjs
    ORDER 1000
    PRIVATE_INCLUDE ${CMAKE_CURRENT_BINARY_DIR})

compat_export()
