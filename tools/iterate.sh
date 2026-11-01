#!/bin/bash
# Rebuild, run headless for N seconds, print the interesting log lines.
#   tools/iterate.sh [seconds] [extra krkrz args...]
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
SEC="${1:-30}"; shift
source "$LLLJ/env.sh"
cmake --build "$LLLJ/krkrz_dev/build/x64-linux" --config Release -j"$(nproc)" 2>&1 | grep -E "error:|FAILED" -A5 | head -40
[ "${PIPESTATUS[0]}" = 0 ] || { echo BUILD FAILED; exit 1; }
pkill -x krkrz 2>/dev/null
LOG="$LLLJ/last.log"
HEADLESS=1 REPLCH="${REPLCH:-$LLLJ/.replch}" timeout "$SEC" "$LLLJ/run.sh" "$@" > "$LOG" 2>&1
echo "exit=$?"
grep -n -E "\(lllj\)|exception|\[dialog\]|Cannot|Error|error|does not exist|失敗" "$LOG" | grep -v "PooledAllocator\|configuration file" | head -${LINES_MAX:-50}
