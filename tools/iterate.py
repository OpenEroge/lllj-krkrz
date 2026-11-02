#!/usr/bin/env python3
"""Short owned headless run; copy its log to last.log for iteration."""
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from scenarios import Session

if __name__ == "__main__":
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 30
    session = Session(tempfile.mkdtemp(prefix="lllj-iterate-"), sys.argv[2:])
    code = None
    try:
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline and session.proc.poll() is None:
            time.sleep(0.1)
        code = session.proc.poll()
    finally:
        session.close()
        shutil.copyfile(session.out / "game.log", ROOT / "last.log")
    lines = (ROOT / "last.log").read_text(errors="replace").splitlines()
    interesting = [line for line in lines if re.search(r"\(lllj\)|exception|\[dialog\]|Cannot|Error|error|does not exist|失敗", line)]
    print("\n".join(interesting[-50:]))
    print(f"log: {ROOT}/last.log; artifacts: {session.out}")
    sys.exit(code or 0)
