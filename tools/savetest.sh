#!/usr/bin/env bash
# The full smoke suite includes asserted ordinary and quick-save round trips.
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
exec "$LLLJ/tools/smoke.sh" "$1" "${2:-8}"
