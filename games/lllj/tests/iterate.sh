#!/bin/bash
# Incremental build + headless run for N seconds + print notable log lines.
#   games/lllj/tests/iterate.sh [seconds]
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
"$ROOT/build.sh" > "$ROOT/build.log" 2>&1 || { tail -60 "$ROOT/build.log"; exit 1; }
exec python3 -I "$ROOT/games/lllj/tests/iterate.py" "$@"
