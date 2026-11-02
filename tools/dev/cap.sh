#!/usr/bin/env bash
# Capture only the game identified by the REPL channel.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
exec python3 -I "$ROOT/tools/dev/capture.py" "${REPLCH:-$ROOT/.replch}" "$1"
