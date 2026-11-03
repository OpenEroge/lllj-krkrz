#!/usr/bin/env python3
"""1080p title/dialogue/ten-transition benchmark; --legacy-index is the control.

--emote samples E-mote scenes instead (batch 2): looping SD scenes, the
title intro and the eyecatch.
--movie samples gallery movie playback (batch 3): the H.264 OP and a WMV3 ED.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
from scenarios import Session, advance, check_log, start_movie


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
    before = session.obj("CompatNative.storageStats()")
    session.ev('CompatNative.resetFrameStats("transitions")')
    for i in range(10):
        session.ev("SystemAction.option()")
        session.wait('SystemHook.currentState == "option"')
        time.sleep(0.5)
        session.jump("option.ks", "*back", "game")
        session.wait('typeof world_object.player == "Object" && kag.inStable')
    stats = session.obj("CompatNative.frameStats()")
    after = session.obj("CompatNative.storageStats()")
    session.steps.append({"name": "transitions", "cycles": 10, "stats": stats,
                          "storage_delta": {key: after[key] - before[key] for key in before}})
    return check_log(session.out / "game.log")


def emote_step(session, name, seconds):
    """session.step plus the E-mote stage times (CompatNative.emoteStats) of that window."""
    session.obj("CompatNative.emoteStats()")  # reset the counters
    session.step(name, seconds)
    session.steps[-1]["emote_stats"] = stats = session.obj("CompatNative.emoteStats()")
    calls = max(1, stats["draw_call_n"])
    print("  per draw ms: " + ", ".join(f"{k}={stats[k + '_ms'] / calls:.2f}" for k in
                                        ("progress", "draw", "readback", "lock", "convert")), flush=True)


def emote_benchmark(session, seconds):
    root = Path(__file__).resolve().parent
    session.boot()
    session.finish_startup()
    time.sleep(3)
    session.ev("kag.setInnerSize(1920,1080)")
    session.jump("title.ks", "*game", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    session.ev(f"Storages.addAutoPath({json.dumps(str(root) + '/')})")
    # SD scenes loop indefinitely: sd105 is the heaviest seen, sd001 typical.
    for name in ("sd105a_ea03_r02", "sd001a01"):
        session.ev(f"tf.llljEv={json.dumps(name)}")
        session.ev('kag.process("emote_test.ks","*ev")')
        time.sleep(1)
        emote_step(session, f"emote_{name}", seconds)
    # One-shot motions: the title intro (~2.75s) and the eyecatch (~3s).
    session.ev('tf.llljEv="title_bg7"')
    session.ev('CompatNative.resetFrameStats("emote_title_intro")')
    session.ev('kag.process("emote_test.ks","*ev")')
    emote_step(session, "emote_title_intro", 2.5)
    session.ev('CompatNative.resetFrameStats("emote_eyecatch")')
    session.ev('kag.process("emote_test.ks","*eyecatch")')
    emote_step(session, "emote_eyecatch", 3)
    return check_log(session.out / "game.log")


def movie_benchmark(session, seconds):
    session.boot()
    session.finish_startup()
    time.sleep(3)
    session.ev("kag.setInnerSize(1920,1080)")
    session.jump("title.ks", "*cgmode", "cgmode")
    for label in ("op", "ed_ena"):
        start_movie(session, label)
        time.sleep(1)
        session.step(f"movie_{label}", seconds)
        session.steps[-1]["movie_stats"] = session.obj("CompatNative.movieStats()")
        session.ev("kag.movies[0].position = kag.movies[0].totalTime - 1000")  # no xdotool input on Wayland
        session.wait('SystemHook.currentState == "cgmode" && kag.conductor.curStorage == "cgmode.ks" && kag.inStable', 20)
    return check_log(session.out / "game.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--legacy-index", action="store_true")
    parser.add_argument("--emote", action="store_true", help="sample E-mote scenes (batch 2)")
    parser.add_argument("--movie", action="store_true", help="sample gallery movies (batch 3)")
    parser.add_argument("--desktop", action="store_true", help="use the current desktop display; audio remains dummy")
    parser.add_argument("--engine-arg", action="append", default=[], help="extra engine option, e.g. -emote=gl")
    args = parser.parse_args()
    extra = (("-autopath_incremental=no",) if args.legacy_index else ()) + tuple(args.engine_arg)
    session = Session(args.out, extra, gallery_seed=args.movie, headless=not args.desktop)
    try:
        run = movie_benchmark if args.movie else emote_benchmark if args.emote else benchmark
        known = run(session, args.seconds)
        session.write_report(passed=True, index="legacy" if args.legacy_index else "incremental", known_exceptions=known)
        print(f"PASS: {session.out}/report.json")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    main()
