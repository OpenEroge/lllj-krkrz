#!/usr/bin/env bash
# Configure after a checkout/move, then build the instrumented Release engine.
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
source "$LLLJ/env.sh"
"$LLLJ/tools/fetch_ffmpeg.sh"
BUILD="$LLLJ/krkrz_dev/build/x64-linux"
CONFIG=(--preset x64-linux -DKRKRZ_LLLJ=ON -DLLLJ_EXTRA_PLUGINS=lllj_compat
    -DKRKRZ_BUILD_TOOLS=OFF -DKRKRZ_USE_ELEMENTS=OFF -DKRKRZ_BUILD_TESTS=OFF
    -DKRKRZ_DRAW_STATS=ON)
if [[ -f "$BUILD/CMakeCache.txt" ]] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$LLLJ/krkrz_dev" "$BUILD/CMakeCache.txt"; then
    CONFIG+=(--fresh)
fi
# Reuse populated FetchContent sources without their stale sub-build caches.
if [[ -d "$BUILD/_deps/harfbuzz_source-src" ]]; then
    CONFIG+=(-DFETCHCONTENT_FULLY_DISCONNECTED=ON)
fi
cd "$LLLJ/krkrz_dev"
cmake "${CONFIG[@]}" "${@}"
cmake --build "$BUILD" --config Release -j"${BUILD_JOBS:-8}"
