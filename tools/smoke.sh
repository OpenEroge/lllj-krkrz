#!/usr/bin/env bash
# tools/smoke.sh OUTDIR [lines=6] [scenario options]
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then "$LLLJ/tools/build.sh"; fi
exec python3 -I "$LLLJ/tools/scenarios.py" "$@"
