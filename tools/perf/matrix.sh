#!/bin/bash
# usage: matrix.sh LABEL [VAR=val ...]   (DESK=1 implied)
cd "$(dirname "$0")/../.."
LABEL=$1; shift
env "$@" DESK=1 timeout 300 python3 tools/perf/ab.py artifacts/skipcpu-v-$LABEL-$(date +%s%N) 15 2>&1 | tail -1 | python3 -c "
import json,sys
d=json.loads(sys.stdin.read())
print('$LABEL'.ljust(8), 'cpu', str(d['game_cpu']).rjust(3), 'draw', str(round(d['threads'].get('DrawThread',0))).rjust(3), 'main', str(round(d['threads'].get('krkrz',0))).rjust(3), 'fps', round(d['fps']), 'pts/s', round(d['points_per_s'],1), 'compose', round(d['compose_ms_per_frame'],2), 'maxint', d['max_interval_ms'], 'aff', ','.join(d['draw_affinity'])[:30], flush=True)"
