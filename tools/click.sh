#!/bin/bash
# Click inside the krkrz window on the headless display.  tools/click.sh X Y [button]
#   or: tools/click.sh key <keysym>
export DISPLAY=:${HEADLESS_DISPLAY:-91}
W=$(xdotool search --pid "$(pgrep -x krkrz | head -1)" 2>/dev/null | tail -1)
[ -z "$W" ] && { echo "no krkrz window"; exit 1; }
xdotool windowmove "$W" 0 0 2>/dev/null
if [ "$1" = key ]; then xdotool windowactivate --sync "$W" 2>/dev/null; xdotool key --window "$W" "$2"; exit; fi
xdotool mousemove --window "$W" "$1" "$2" sleep 0.1 click "${3:-1}"
