#!/usr/bin/env python3
"""1080p title/dialogue/ten-transition benchmark; --legacy-index is the control."""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenarios import Session, advance, check_log


def benchmark(session, seconds):
    session.boot()
    session.finish_startup()
    time.sleep(3)
    session.ev("kag.setInnerSize(1920,1080)")
    session.step("title", seconds)
    session.jump("title.ks", "*game", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(session, 6)
    session.step("dialogue", seconds)
    before = session.obj("LLLJNative.storageStats()")
    session.ev('LLLJNative.resetFrameStats("transitions")')
    for i in range(10):
        session.ev("SystemAction.option()")
        session.wait('SystemHook.currentState == "option"')
        time.sleep(0.5)
        session.jump("option.ks", "*back", "game")
        session.wait('typeof world_object.player == "Object" && kag.inStable')
    stats = session.obj("LLLJNative.frameStats()")
    after = session.obj("LLLJNative.storageStats()")
    session.steps.append({"name": "transitions", "cycles": 10, "stats": stats,
                          "storage_delta": {key: after[key] - before[key] for key in before}})
    return check_log(session.out / "game.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--legacy-index", action="store_true")
    parser.add_argument("--desktop", action="store_true", help="use the current desktop display; audio remains dummy")
    args = parser.parse_args()
    session = Session(args.out, ("-lllj_index=no",) if args.legacy_index else (), headless=not args.desktop)
    try:
        known = benchmark(session, args.seconds)
        session.write_report(passed=True, index="legacy" if args.legacy_index else "incremental", known_exceptions=known)
        print(f"PASS: {session.out}/report.json")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    main()
