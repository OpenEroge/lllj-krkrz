#!/bin/bash
# Recreate the run directory game/ from the unpacked data (result/).
#   games/lllj/setup.sh [RESULT_DIR]
# game/ holds symlinks to result/data/* and result/<archive>/, plus a plain
# patch.xp3 repacked from result/patch (the game mounts patch.xp3 itself) and a link
# to the Chinese patch pack lllj_zh.pck.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
R="${1:-$ROOT/../limelight/result}"
if [ ! -d "$R/data" ] || [ ! -d "$R/patch" ]; then
	echo "Missing unpacked game data in $R (expected data/ and patch/)" >&2
	exit 1
fi
R="$(cd "$R" && pwd)"
G="$ROOT/game"  # RUN_DIR in game.conf
mkdir -p "$G"
for f in "$R"/data/*; do
	b=$(basename "$f")
	case "$b" in 2EA4AAEC*|'startup.tjs~1') continue;; esac
	ln -sfnT --relative "$f" "$G/$b"
done
for a in bgimage bgm evimage evimage2 fgimage image scn uipsd video voice voice2; do
	ln -sfnT --relative "$R/$a" "$G/$a"
done
python3 -I "$ROOT/tools/formats/xp3plain.py" --if-stale "$R/patch" "$G/patch.xp3"
if [[ -d "$R/unencrypted" ]]; then
	python3 -I "$ROOT/tools/formats/xp3plain.py" --if-stale "$R/unencrypted" "$G/patch2.xp3"
else
	echo "Missing unencrypted/ (53 replacement CG files expected)" >&2
	exit 1
fi
# Chinese patch pack, read in place (modules/xpck-patch, games/lllj/zh-patch.toml)
if [[ -f "$R/../lllj_zh.pck" ]]; then
	ln -sfnT --relative "$R/../lllj_zh.pck" "$G/lllj_zh.pck"
fi
echo "game/ ready"
