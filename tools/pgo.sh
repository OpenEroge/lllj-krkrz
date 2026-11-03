#!/usr/bin/env bash
# Profile-guided optimization of the Release build.
#   tools/pgo.sh [extra cmake configure args for the final build...]
#
# 1. Builds the instrumented engine (preset linux-x64-pgo-gen, build/linux-x64-pgo-gen,
#    LLLJ_PGO=generate, no LTO).
# 2. Trains it headless (Xvfb, see run.sh) with the existing test drivers. Their
#    pass/fail is ignored: the instrumented binary is slower than the real one.
#    Each run must end with a normal exit, which is when the counters are written.
#      perf/ab.py      60 s of skip: TJS VM, KAG, text rendering, layer compositing
#      perf/menu.py    settings screen open/close (UI construction)
#      run.py          the smoke, zh and movie test cases, and emote on the GL backend
# 3. Rebuilds build/linux-x64 with LLLJ_PGO=use (kept in its cache; ./build.sh -DLLLJ_PGO=
#    turns it off again).
# env: BUILD_JOBS, VCPKG_ROOT (as build.sh), LLLJ_PGO_DIR (default build/pgo-profile),
#      PGO_OUT (training output, default artifacts/pgo-training)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE="${LLLJ_PGO_DIR:-$ROOT/build/pgo-profile}"
OUT="${PGO_OUT:-$ROOT/artifacts/pgo-training}"
TESTS="$ROOT/games/lllj/tests"

echo "== instrumented build"
PRESET=linux-x64-pgo-gen "$ROOT/build.sh" -DLLLJ_PGO_DIR="$PROFILE"

echo "== training (profile: $PROFILE)"
rm -rf "$PROFILE" "$OUT"
mkdir -p "$OUT"
export KRKRZ_BIN_DIR="$ROOT/build/linux-x64-pgo-gen/core/Release"
export KRKRZ_EXIT_TIMEOUT=120  # time to write the profile at exit
train() {
	local name=$1; shift
	local start=$SECONDS
	if "$@" > "$OUT/$name.out" 2>&1; then r=ok; else r="failed (ignored)"; fi
	printf '%-8s %s (%ds), %d profile files\n' "$name" "$r" $((SECONDS - start)) \
		"$(find "$PROFILE" -name '*.gcda' 2>/dev/null | wc -l)"
}
cd "$ROOT"
train skip   python3 -I "$TESTS/perf/ab.py" "$OUT/skip" 60
train menu   python3 -I "$TESTS/perf/menu.py" "$OUT/menu" 10
train cases  python3 -I "$TESTS/run.py" -o "$OUT/cases" -j 4 smoke zh movie
train emote  python3 -I "$TESTS/run.py" -o "$OUT/emote" -j 4 emote --engine-arg=-emote=gl
if [[ -z "$(find "$PROFILE" -name '*.gcda' -print -quit 2>/dev/null)" ]]; then
	echo "no profile was written to $PROFILE (did the game exit normally?)" >&2
	exit 1
fi

echo "== optimized build"
"$ROOT/build.sh" -DLLLJ_PGO=use -DLLLJ_PGO_DIR="$PROFILE" "$@"
