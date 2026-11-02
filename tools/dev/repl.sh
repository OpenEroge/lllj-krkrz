#!/usr/bin/env bash
# A single lockstep expression. Never accept modal dialogs implicitly.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
exec python3 -I "$ROOT/tools/dev/repl.py" "$@"
