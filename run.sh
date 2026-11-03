#!/bin/bash
# Run a game on the engine built by ./build.sh.
#   ./run.sh [engine options...]            GAME=<profile> selects games/<profile>/ (default lllj)
# env: HEADLESS=1  -> private Xvfb display + dummy audio (for automated tests)
#      REPLCH=dir  -> enable the REPL file channel in dir (tools/dev/repl.sh)
#      KRKRZ_BIN_DIR=dir -> run the krkrz/libSDL3 from dir instead of the Release build
#                    (e.g. the PGO training build, or a saved binary for A/B runs)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
GAME="${GAME:-lllj}"
source "$ROOT/games/$GAME/game.conf"
BIN="${KRKRZ_BIN_DIR:-$ROOT/build/linux-x64/core/Release}"
RUN="$ROOT/$RUN_DIR"
mkdir -p "$RUN"
# Atomic replacement permits rebuilding while another isolated instance runs.
EXE_TMP=$(mktemp "$RUN/.krkrz.XXXXXX")
cp "$BIN/krkrz" "$EXE_TMP"
chmod 755 "$EXE_TMP"
mv -f "$EXE_TMP" "$RUN/krkrz"
for so in "$BIN"/libSDL3.so.*.*; do
	SO_TMP=$(mktemp "$RUN/.sdl3.XXXXXX")
	cp "$so" "$SO_TMP"
	chmod 644 "$SO_TMP"
	mv -f "$SO_TMP" "$RUN/$(basename "$so")"
	ln -sf "$(basename "$so")" "$RUN/libSDL3.so.0"
done
# prebuilt FFmpeg for the movie backend (tools/fetch_ffmpeg.sh); found via the
# executable's $ORIGIN rpath so the system libraries with the same soname are not used
for so in avformat.so.62 avcodec.so.62 avutil.so.60 swresample.so.6; do
	SRC=$(readlink -f "$ROOT/third_party/ffmpeg/lib/lib$so")
	SO_TMP=$(mktemp "$RUN/.ffmpeg.XXXXXX")
	cp "$SRC" "$SO_TMP"
	chmod 644 "$SO_TMP"
	mv -f "$SO_TMP" "$RUN/lib$so"
done
for pd in "${PLACEHOLDER_DIRS[@]}"; do
	mkdir -p "$RUN/$pd"
	for d in "${PLACEHOLDER_DLLS[@]}"; do : > "$RUN/$pd/$d.dll"; done
done
game_prepare "$@"
ARGS=("$RUN/" "${ENGINE_ARGS[@]}")
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
	cd "$RUN"
	./krkrz "${ARGS[@]}" "$@" &
	GAME_PID=$!
	if [[ -n "${REPLCH:-}" ]]; then
		printf '%s\n' "$DISP" > "$REPLCH/display"
		printf '%s\n' "$GAME_PID" > "$REPLCH/pid"
	fi
	wait "$GAME_PID"
	exit $?
fi
cd "$RUN"
if [[ -n "${REPLCH:-}" ]]; then
	printf '%s\n' "${DISPLAY:-}" > "$REPLCH/display"
	printf '%s\n' "$$" > "$REPLCH/pid"
fi
exec ./krkrz "${ARGS[@]}" "$@"
