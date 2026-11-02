#!/usr/bin/env bash
# Capture only the game identified by the REPL channel.
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 -I "$LLLJ/tools/capture.py" "${REPLCH:-$LLLJ/.replch}" "$1"
