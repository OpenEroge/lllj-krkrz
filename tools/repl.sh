#!/bin/bash
# Send one TJS expression through the krkrz -replfile channel and print the JSON reply.
#   tools/repl.sh CHANNEL_DIR 'expr' [timeout_sec]
CH="$1"; EXPR="$2"; T="${3:-20}"
rm -f "$CH/resp"
printf '%s' "$EXPR" > "$CH/cmd.tmp" && mv -f "$CH/cmd.tmp" "$CH/cmd"
for ((i=0; i<T*20; i++)); do
	if [ -f "$CH/modal" ]; then cat "$CH/modal"; echo; printf 'yes' > "$CH/modalresp"; fi
	if [ -f "$CH/resp" ]; then sleep 0.05; cat "$CH/resp"; echo; rm -f "$CH/resp"; exit 0; fi
	sleep 0.05
done
echo "<TIMEOUT>"; exit 1
