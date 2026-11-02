#!/usr/bin/env python3
"""Short owned headless run; copy its log to last.log for iteration."""
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
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
    interesting = [line for line in lines if re.search(r"\((lllj|compat)\)|exception|\[dialog\]|Cannot|Error|error|does not exist|失敗", line)]
    print("\n".join(interesting[-50:]))
    print(f"log: {ROOT}/last.log; artifacts: {session.out}")
    sys.exit(code or 0)
