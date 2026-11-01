#!/bin/bash
# Run Limelight Lemonade Jam on the krkrz_dev (SDL3) build.
#   ./run.sh [extra krkrz options...]
# env: HEADLESS=1  -> private Xvfb display + dummy audio (for automated tests)
#      REPLCH=dir  -> enable the REPL file channel in dir
set -e
LLLJ="$(cd "$(dirname "$0")" && pwd)"
BIN="$LLLJ/krkrz_dev/build/x64-linux/core/Release"
GAME="$LLLJ/game"
# exePath must be the game dir (arcPath -> patch.xp3, plugin/ placeholders)
cp -f "$BIN/krkrz" "$GAME/krkrz"
for so in "$BIN"/libSDL3.so.*.*; do
	cp -f "$so" "$GAME/"
	ln -sf "$(basename "$so")" "$GAME/libSDL3.so.0"
done
# placeholder files so the game's CanLoadPlugin() finds the DLLs we emulate
for pd in plugin plugin64; do
	mkdir -p "$GAME/$pd"
	for d in PackinOne KAGParserEx wuvorbis wuopus win32dialog AlphaMovie fstat; do
		: > "$GAME/$pd/$d.dll"
	done
done
ARGS=("$GAME/" -loglevel=INFO -used2d=no -debugwin=no)
[ -n "$REPLCH" ] && { mkdir -p "$REPLCH"; rm -f "$REPLCH"/{cmd,cmd.tmp,resp}; ARGS+=("-replfile=$REPLCH"); }
if [ -n "$HEADLESS" ]; then
	DISP=:${HEADLESS_DISPLAY:-91}
	if ! [ -e /tmp/.X11-unix/X${DISP#:} ]; then
		Xvfb $DISP -screen 0 2560x1600x24 -nolisten tcp >/dev/null 2>&1 &
		sleep 1
	fi
	export DISPLAY=$DISP SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy
	unset WAYLAND_DISPLAY
fi
cd "$GAME"
exec ./krkrz "${ARGS[@]}" "$@"
