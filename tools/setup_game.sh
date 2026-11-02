#!/bin/bash
# Recreate the run directory game/ from the unpacked data (result/).
#   tools/setup_game.sh [RESULT_DIR]
# game/ holds symlinks to result/data/* and result/<archive>/, plus a plain
# patch.xp3 repacked from result/patch (the game mounts patch.xp3 itself).
set -e
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
R="${1:-$LLLJ/../limelight/result}"
if [ ! -d "$R/data" ] || [ ! -d "$R/patch" ]; then
	echo "Missing unpacked game data in $R (expected data/ and patch/)" >&2
	exit 1
fi
R="$(cd "$R" && pwd)"
G="$LLLJ/game"
mkdir -p "$G"
for f in "$R"/data/*; do
	b=$(basename "$f")
	case "$b" in 2EA4AAEC*|'startup.tjs~1') continue;; esac
	ln -sfnT --relative "$f" "$G/$b"
done
for a in bgimage bgm evimage evimage2 fgimage image scn uipsd video voice voice2; do
	ln -sfnT --relative "$R/$a" "$G/$a"
done
python3 -I "$LLLJ/tools/xp3plain.py" --if-stale "$R/patch" "$G/patch.xp3"
if [[ -d "$R/unencrypted" ]]; then
	python3 -I "$LLLJ/tools/xp3plain.py" --if-stale "$R/unencrypted" "$G/patch2.xp3"
else
	echo "Missing unencrypted/ (53 replacement CG files expected)" >&2
	exit 1
fi
echo "game/ ready"
