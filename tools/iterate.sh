#!/usr/bin/env bash
# Build and run only this test instance for N seconds.
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
"$LLLJ/tools/build.sh" > "$LLLJ/build.log" 2>&1 || { tail -60 "$LLLJ/build.log"; exit 1; }
exec python3 -I "$LLLJ/tools/iterate.py" "$@"
