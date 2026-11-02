#!/usr/bin/env bash
# Pointer/key input routed only to the instance identified by REPLCH.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CH="${REPLCH:-$ROOT/.replch}"
export DISPLAY="$(cat "$CH/display")"
GAME_PID="$(cat "$CH/pid")"
W=$(xdotool search --onlyvisible --pid "$GAME_PID" | tail -1)
[[ -n "$W" ]] || { echo 'No game window in this REPL channel' >&2; exit 1; }
xdotool windowmove "$W" 0 0
xdotool windowfocus --sync "$W"
if [[ "$1" == key ]]; then
    # XTest updates the server/SDL modifier state; --window uses SendEvent and
    # can leave synthetic modifiers latched when fullscreen changes focus.
    xdotool key "$2"
else
    xdotool mousemove --window "$W" "$1" "$2" sleep 0.1 click "${3:-1}"
fi
