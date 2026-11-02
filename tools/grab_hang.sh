#!/bin/bash
# Capture state of a hung krkrz: thread stacks (3 samples), CPU per thread, memory.
OUT=${1:-$(dirname "$0")/hang-$(date +%H%M%S)}; mkdir -p "$OUT"
PID=${PID:-$(pgrep -f '/game/krkrz' | head -1)}; [ -z "$PID" ] && { echo "no krkrz running"; exit 1; }
echo "pid=$PID out=$OUT"
top -H -b -n1 -p "$PID" > "$OUT/threads.txt"
grep -E 'VmRSS|VmSize|Threads' /proc/$PID/status > "$OUT/status.txt"
for i in 1 2 3; do
  gdb -p "$PID" -batch -ex 'set pagination off' -ex 'thread apply all bt 40' > "$OUT/bt$i.txt" 2>&1
  sleep 1
done
echo done; ls "$OUT"
