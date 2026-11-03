#!/usr/bin/env bash
# Full regression: every scenario case, in parallel (games/lllj/tests/run.py all).
#   games/lllj/tests/regress.sh OUTDIR [run.py options...]
# Only for a batch of changes or on request; after an ordinary edit use
#   games/lllj/tests/run.py --changed
set -euo pipefail
OUT="${1:?usage: games/lllj/tests/regress.sh OUTDIR [run.py options...]}"; shift
exec python3 -I "$(dirname "$0")/run.py" all -o "$OUT" "$@"
