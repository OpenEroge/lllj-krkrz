#!/usr/bin/env bash
# A single lockstep expression. Never accept modal dialogs implicitly.
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 -I "$LLLJ/tools/repl.py" "$@"
