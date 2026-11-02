#!/usr/bin/env python3
"""File-channel client shared by interactive tools and scenario tests."""
import fcntl
import json
from pathlib import Path
import sys
import time


def evaluate(channel, expression, timeout=20, modal_answer=None):
    channel = Path(channel)
    if not channel.is_dir():
        raise RuntimeError(f"REPL channel does not exist: {channel}")
    with (channel / "client.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for name in ("resp", "cmd", "cmd.tmp"):
            (channel / name).unlink(missing_ok=True)
        (channel / "cmd.tmp").write_text(expression, encoding="utf-8")
        (channel / "cmd.tmp").replace(channel / "cmd")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if (channel / "modal").exists() and not (channel / "modalresp").exists():
                prompt = (channel / "modal").read_text()
                if modal_answer is None:
                    raise RuntimeError(f"Unexpected modal: {prompt}")
                (channel / "modalresp").write_text(modal_answer)
            response = channel / "resp"
            if response.exists():
                data = json.loads(response.read_text())
                response.unlink()
                if not data.get("ok"):
                    raise RuntimeError(data.get("error") or str(data))
                return data
            time.sleep(0.025)
        raise TimeoutError(f"REPL timed out after {timeout}s: {expression[:120]}")


if __name__ == "__main__":
    try:
        print(json.dumps(evaluate(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 20), ensure_ascii=False))
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        sys.exit(1)
