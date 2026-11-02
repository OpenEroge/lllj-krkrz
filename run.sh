#!/bin/bash
# Run Limelight Lemonade Jam on the krkrz_dev (SDL3) build.
#   ./run.sh [extra krkrz options...]
# env: HEADLESS=1  -> private Xvfb display + dummy audio (for automated tests)
#      REPLCH=dir  -> enable the REPL file channel in dir
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")" && pwd)"
BIN="$LLLJ/krkrz_dev/build/x64-linux/core/Release"
GAME="$LLLJ/game"
# exePath must be the game dir (arcPath -> patch.xp3, plugin/ placeholders)
mkdir -p "$GAME"
# Atomic replacement permits rebuilding while another isolated instance runs.
EXE_TMP=$(mktemp "$GAME/.krkrz.XXXXXX")
cp "$BIN/krkrz" "$EXE_TMP"
chmod 755 "$EXE_TMP"
mv -f "$EXE_TMP" "$GAME/krkrz"
for so in "$BIN"/libSDL3.so.*.*; do
	SO_TMP=$(mktemp "$GAME/.sdl3.XXXXXX")
	cp "$so" "$SO_TMP"
	chmod 644 "$SO_TMP"
	mv -f "$SO_TMP" "$GAME/$(basename "$so")"
	ln -sf "$(basename "$so")" "$GAME/libSDL3.so.0"
done
# placeholder files so the game's CanLoadPlugin() finds the DLLs we emulate
for pd in plugin plugin64; do
	mkdir -p "$GAME/$pd"
	for d in PackinOne KAGParserEx wuvorbis wuopus win32dialog AlphaMovie fstat motionplayer_nod3d; do
		: > "$GAME/$pd/$d.dll"
	done
done
ARGS=("$GAME/" -loglevel=INFO -used2d=no -debugwin=no)
HAS_DATAPATH=false
for ARG in "$@"; do [[ "$ARG" == -datapath=* ]] && HAS_DATAPATH=true; done
if ! "$HAS_DATAPATH"; then
	ARGS+=("-datapath=${XDG_DATA_HOME:-$HOME/.local/share}/limelight_lj/")
fi
if [ -n "${REPLCH:-}" ]; then
	mkdir -p "$REPLCH"
	rm -f "$REPLCH"/{cmd,cmd.tmp,resp,modal,modalresp}
	ARGS+=("-replfile=$REPLCH")
fi
if [ -n "${HEADLESS:-}" ]; then
	XVFB_PID=""
	GAME_PID=""
	DISPLAY_FILE=""
	cleanup() {
		if [[ -n "$GAME_PID" ]]; then
			kill "$GAME_PID" 2>/dev/null || true
			wait "$GAME_PID" 2>/dev/null || true
		fi
		if [[ -n "$XVFB_PID" ]]; then
			kill "$XVFB_PID" 2>/dev/null || true
			wait "$XVFB_PID" 2>/dev/null || true
		fi
		[[ -z "$DISPLAY_FILE" ]] || rm -f "$DISPLAY_FILE"
	}
	trap cleanup EXIT
	trap 'exit 130' INT
	trap 'exit 143' TERM
	if [[ -n "${HEADLESS_DISPLAY:-}" ]]; then
		DISP=:$HEADLESS_DISPLAY
		# An explicitly supplied display belongs to the caller.
	else
		DISPLAY_FILE=$(mktemp)
		XVFB_LOG=/dev/null
		[[ -z "${REPLCH:-}" ]] || XVFB_LOG="$REPLCH/xvfb.log"
		Xvfb -displayfd 3 -screen 0 2560x1600x24 -nolisten tcp 3>"$DISPLAY_FILE" >"$XVFB_LOG" 2>&1 &
		XVFB_PID=$!
		for ((i=0; i<100; i++)); do
			[[ -s "$DISPLAY_FILE" ]] && break
			kill -0 "$XVFB_PID" 2>/dev/null || { echo 'Xvfb failed' >&2; exit 1; }
			sleep 0.05
		done
		[[ -s "$DISPLAY_FILE" ]] || { echo 'Xvfb startup timeout' >&2; exit 1; }
		DISP=:$(cat "$DISPLAY_FILE")
	fi
	export DISPLAY=$DISP SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy
	unset WAYLAND_DISPLAY
	cd "$GAME"
	./krkrz "${ARGS[@]}" "$@" &
	GAME_PID=$!
	if [[ -n "${REPLCH:-}" ]]; then
		printf '%s\n' "$DISP" > "$REPLCH/display"
		printf '%s\n' "$GAME_PID" > "$REPLCH/pid"
	fi
	wait "$GAME_PID"
	exit $?
fi
cd "$GAME"
if [[ -n "${REPLCH:-}" ]]; then
	printf '%s\n' "${DISPLAY:-}" > "$REPLCH/display"
	printf '%s\n' "$$" > "$REPLCH/pid"
fi
exec ./krkrz "${ARGS[@]}" "$@"
