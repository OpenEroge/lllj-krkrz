#!/bin/bash
# Quick-save / quick-load round trip on top of smoke.sh.  tools/savetest.sh OUTDIR
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$1"
"$LLLJ/tools/smoke.sh" "$OUT" 8 >/dev/null
T="$LLLJ/tools"
"$T/click.sh" 626 790; sleep 2; "$T/click.sh" 590 473; sleep 3          # Q.SAVE -> はい
"$T/cap.sh" "$OUT/20_saved.png" >/dev/null
"$T/repl.sh" "$LLLJ/.replch" "kag.conductor.curStorage + ':' + kag.conductor.curLine" 5
for i in 1 2 3 4 5; do "$T/click.sh" 720 300; sleep 1.5; done
"$T/cap.sh" "$OUT/21_advanced.png" >/dev/null
"$T/repl.sh" "$LLLJ/.replch" "kag.conductor.curStorage + ':' + kag.conductor.curLine" 5
"$T/click.sh" 698 790; sleep 2; "$T/click.sh" 590 473; sleep 5          # Q.LOAD -> はい
"$T/cap.sh" "$OUT/22_loaded.png" >/dev/null
"$T/repl.sh" "$LLLJ/.replch" "kag.conductor.curStorage + ':' + kag.conductor.curLine" 5
ls -la --time-style=+%T ~/.local/share/wamsoft/krkrz/ | grep -i quick
grep -n -E "exception occured|\[dialog\]|失敗" "$OUT/game.log" | tail -6 | cut -c1-180
pgrep -x krkrz >/dev/null && echo ALIVE || echo DEAD
