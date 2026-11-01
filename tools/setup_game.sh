#!/bin/bash
# Recreate the run directory game/ from the unpacked data (result/).
#   tools/setup_game.sh [RESULT_DIR]
# game/ holds symlinks to result/data/* and result/<archive>/, plus a plain
# patch.xp3 repacked from result/patch (the game mounts patch.xp3 itself).
set -e
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
R="${1:-/run/host/var/home/aoba/Downloads/limelight_lj_wm (Copy 2)/result}"
G="$LLLJ/game"
mkdir -p "$G"
for f in "$R"/data/*; do
	b=$(basename "$f")
	case "$b" in 2EA4AAEC*|'startup.tjs~1') continue;; esac
	ln -sfn "$f" "$G/$b"
done
for a in bgimage bgm evimage evimage2 fgimage image scn uipsd video voice voice2; do
	ln -sfn "$R/$a" "$G/$a"
done
[ -f "$G/patch.xp3" ] || python3 -I "$LLLJ/tools/xp3plain.py" "$R/patch" "$G/patch.xp3"
echo "game/ ready"
