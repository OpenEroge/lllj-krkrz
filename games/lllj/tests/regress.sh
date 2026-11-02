#!/usr/bin/env bash
# Full regression: every scenario suite, the Windows save import and the
# Japanese-only smoke, each in its own isolated run directory.
#   games/lllj/tests/regress.sh OUTDIR [suite...]      (default: all suites)
# Suites: smoke emote movie audio extras zh winsave smoke-jp
# Exit status is non-zero when any suite fails; OUTDIR/summary.txt lists results.
set -uo pipefail
TESTS="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:?usage: games/lllj/tests/regress.sh OUTDIR [suite...]}"; shift
SUITES=("$@")
[[ ${#SUITES[@]} -gt 0 ]] || SUITES=(smoke emote movie audio extras zh winsave smoke-jp)
SCEN="$TESTS/scenarios.py"
mkdir -p "$OUT"
: > "$OUT/summary.txt"
fail=0
for s in "${SUITES[@]}"; do
	case "$s" in
		smoke)    cmd=(python3 -I "$SCEN" "$OUT/$s" 8) ;;
		smoke-jp) cmd=(python3 -I "$SCEN" "$OUT/$s" 8 --engine-arg=-lllj_zh=no) ;;
		winsave)  cmd=(python3 -I "$TESTS/verify_windows_saves.py" "$OUT/$s") ;;
		*)        cmd=(python3 -I "$SCEN" "$OUT/$s" --only "$s") ;;
	esac
	start=$SECONDS
	if "${cmd[@]}" > "$OUT/$s.out" 2>&1; then r=PASS; else r=FAIL; fail=1; fi
	printf '%-9s %s (%ds)\n' "$s" "$r" $((SECONDS - start)) | tee -a "$OUT/summary.txt"
done
exit $fail
