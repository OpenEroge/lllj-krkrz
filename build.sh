#!/usr/bin/env bash
# Configure (first run or after moving the checkout) and build the Release engine.
#   ./build.sh [extra cmake configure args...]
# env: BUILD_JOBS (default 8), VCPKG_ROOT (default ~/vcpkg),
#      PRESET (default linux-x64; linux-x64-pgo-gen is the PGO training build, see tools/pgo.sh)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# Keep foreign toolchains (e.g. linuxbrew) out of the build.
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export VCPKG_FORCE_SYSTEM_BINARIES=1
export CMAKE_POLICY_VERSION_MINIMUM=3.5
unset PKG_CONFIG_PATH LD_LIBRARY_PATH CPATH LIBRARY_PATH

"$ROOT/tools/fetch_ffmpeg.sh"
PRESET="${PRESET:-linux-x64}"
BUILD="$ROOT/build/$PRESET"
CONFIG=(--preset "$PRESET")
if [[ -f "$BUILD/CMakeCache.txt" ]] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$ROOT" "$BUILD/CMakeCache.txt"; then
    CONFIG+=(--fresh)
fi
# Reuse populated FetchContent sources without their stale sub-build caches.
if [[ -d "$BUILD/_deps/harfbuzz_source-src" ]]; then
    CONFIG+=(-DFETCHCONTENT_FULLY_DISCONNECTED=ON)
fi
cd "$ROOT"
cmake "${CONFIG[@]}" "$@"
cmake --build "$BUILD" --config Release -j"${BUILD_JOBS:-8}"
