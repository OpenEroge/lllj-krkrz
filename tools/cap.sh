#!/bin/bash
# Screenshot the running game via the REPL channel. tools/cap.sh OUT.png
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
rm -f "$1"
"$LLLJ/tools/repl.sh" "${REPLCH:-$LLLJ/.replch}" "System.captureScreen('$1')" 10 >/dev/null
for i in $(seq 50); do [ -s "$1" ] && break; sleep 0.1; done
ls -la "$1"
