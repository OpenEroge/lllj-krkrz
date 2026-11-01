#!/bin/bash
# Headless smoke test: boot -> ATTENTION -> title -> はじめから -> advance N lines -> screenshots.
#   tools/smoke.sh OUTDIR [lines=6]
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"; N="${2:-6}"; mkdir -p "$OUT"
export REPLCH="$LLLJ/.replch"
pkill -x krkrz 2>/dev/null; sleep 0.5
source "$LLLJ/env.sh"
cmake --build "$LLLJ/krkrz_dev/build/x64-linux" --config Release -j"$(nproc)" 2>&1 | grep -E "error:" -A5 | head -20
( HEADLESS=1 timeout 600 "$LLLJ/run.sh" > "$OUT/game.log" 2>&1 & )
for i in $(seq 60); do grep -q "Revision:" "$OUT/game.log" 2>/dev/null && break; sleep 0.5; done
sleep 4
"$LLLJ/tools/cap.sh" "$OUT/01_attention.png" >/dev/null
"$LLLJ/tools/click.sh" 720 400; sleep 7
"$LLLJ/tools/cap.sh" "$OUT/02_title.png" >/dev/null
"$LLLJ/tools/click.sh" 212 771; sleep 8
"$LLLJ/tools/cap.sh" "$OUT/03_start.png" >/dev/null
for i in $(seq "$N"); do "$LLLJ/tools/click.sh" 720 400; sleep 2.5; done
"$LLLJ/tools/cap.sh" "$OUT/04_scene.png" >/dev/null
grep -n -E "exception occured|\[dialog\]|\(lllj\) stub" "$OUT/game.log" | cut -c1-160 | tail -20
ls "$OUT"
