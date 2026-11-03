#!/usr/bin/env python3
"""Open and close the in-game settings screen N times; report main-thread CPU per cycle.

  python3 -I games/lllj/tests/perf/menu.py OUT [N]

The settings screen copies many class members when it is built, so this is the
allocator-heavy path from the profiling notes. Wall time per cycle includes the
REPL polling (0.1 s steps); main-thread CPU seconds per cycle is the number to compare.
env: EXTRA="..." extra engine options, DESK=1 run on the current desktop.
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # games/lllj/tests
from scenarios import Session, advance

HZ = os.sysconf("SC_CLK_TCK")


def ticks(pid):
    """CPU ticks of the main thread (named krkrz) and of every thread together."""
    main = total = 0
    for t in Path(f"/proc/{pid}/task").iterdir():
        try:
            st = (t / "stat").read_text()
        except OSError:
            continue
        name = st[st.index("(") + 1:st.rindex(")")]
        f = st[st.rindex(")") + 2:].split()
        n = int(f[11]) + int(f[12])
        if not name.startswith("llvmpipe"):
            total += n
        if name == "krkrz":
            main += n
    return main, total


def main():
    out = Path(sys.argv[1])
    cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    desk = bool(os.environ.get("DESK"))
    s = Session(out, extra=tuple(os.environ.get("EXTRA", "").split()), headless=not desk)
    try:
        s.boot()
        s.finish_startup()
        time.sleep(2)
        if not desk:
            s.ev("kag.setInnerSize(1920,1080)")
        s.jump("title.ks", "*game", "game")
        s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
        advance(s, 2)
        s.wait("kag.inStable")
        time.sleep(1)
        pid = int(subprocess.check_output(["pgrep", "-n", "-f", f"datapath={s.out}/saves/"]).split()[0])
        # One warm-up cycle so first-use loads are not counted.
        for i in range(cycles + 1):
            if i == 1:
                s.ev('CompatNative.resetFrameStats("menu")')
                m0, a0 = ticks(pid)
                t0 = time.monotonic()
            s.ev("SystemAction.option()")
            s.wait('SystemHook.currentState == "option" && kag.inStable')
            s.jump("option.ks", "*back", "game")
            s.wait('typeof world_object.player == "Object" && SystemHook.currentState == "game" && kag.inStable')
        wall = time.monotonic() - t0
        m1, a1 = ticks(pid)
        st = s.obj("CompatNative.frameStats()")
        r = dict(cycles=cycles, extra=os.environ.get("EXTRA", ""),
                 main_cpu_ms_per_cycle=round(1000 * (m1 - m0) / HZ / cycles, 1),
                 game_cpu_ms_per_cycle=round(1000 * (a1 - a0) / HZ / cycles, 1),
                 wall_ms_per_cycle=round(1000 * wall / cycles),
                 max_interval_ms=round(st["max_interval_ms"], 1), fps=round(st["fps"], 1))
        (out / "menu.json").write_text(json.dumps(r, indent=2))
        print(json.dumps(r, ensure_ascii=False))
    finally:
        s.close()


if __name__ == "__main__":
    main()
