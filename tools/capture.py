#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from repl import evaluate

if __name__ == "__main__":
    target = Path(sys.argv[2]).resolve()
    target.unlink(missing_ok=True)
    evaluate(sys.argv[1], f"System.captureScreen({json.dumps(str(target))})", 10)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if target.exists() and target.stat().st_size > 24:
            print(target)
            break
        time.sleep(0.05)
    else:
        raise TimeoutError(f"Screenshot was not written: {target}")
