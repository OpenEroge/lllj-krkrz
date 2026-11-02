#!/usr/bin/env python3
"""Sound label scheduling stress: fails if the main thread stops answering.

Regression check for the lock-order deadlock between the decode thread
(buffer lock -> label schedule) and the sound event thread (buffer list ->
buffer lock). Usage: audio_lock_stress.py OUT [seconds]
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from scenarios import Session

SOUND = "パソコン／キーボード.ogg"  # its .sli has Label Position=910336 (~20.6s @44.1kHz)
LABEL_MS = 20640


def main():
    out = Path(sys.argv[1])
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    session = Session(out, ("-nostartup",))
    try:
        session.wait('typeof global.LLLJNative == "Object"', 60)
        session.ev(f"Scripts.execStorage({json.dumps(str(ROOT / 'tools/audio_lock_stress.tjs'))})")
        buffers = int(os.environ.get("NBUF", 16))
        position = LABEL_MS - int(os.environ.get("AHEAD", 1000))
        interval = int(os.environ.get("IVL", 1100))
        count = session.ev(f"llljStressStart({json.dumps(SOUND, ensure_ascii=False)}, {buffers}, {position}, {interval})")
        print(f"{count} buffers", flush=True)
        start = time.monotonic()
        while time.monotonic() - start < seconds:
            try:
                plays = session.ev("llljStress.plays + \" labels=\" + llljStress.labels", timeout=10)
            except TimeoutError:
                pid = (session.channel / "pid").read_text().strip()
                print(f"RED: main thread stalled after {time.monotonic() - start:.1f}s", flush=True)
                subprocess.run([str(ROOT / "tools/grab_hang.sh"), str(out / "hang")], env={"PID": pid, "PATH": "/usr/bin:/bin"})
                return 1
            print(f"{time.monotonic() - start:5.1f}s plays={plays}", flush=True)
            time.sleep(1)
        print("GREEN")
        return 0
    finally:
        session.close()


if __name__ == "__main__":
    sys.exit(main())
