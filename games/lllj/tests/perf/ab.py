import sys, time, os, subprocess, json
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # games/lllj/tests
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../tools/dev")))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../..")))
from scenarios import Session, advance
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "cpuutil.py")).read())
out = Path(sys.argv[1]); secs = float(sys.argv[2])
DESK = bool(os.environ.get("DESK"))
s = Session(out, extra=tuple(os.environ.get("EXTRA","").split()), headless=not DESK)
try:
    s.boot(); s.finish_startup(); time.sleep(2)
    if not DESK: s.ev("kag.setInnerSize(1920,1080)")
    if os.environ.get("FULL"): s.ev("kag.fullScreen = true"); time.sleep(2)
    s.jump("title.ks", "*game", "game")
    s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(s, 2); time.sleep(1)
    pid = int(subprocess.check_output(["pgrep", "-n", "-f", f"datapath={s.out}/saves/"]).split()[0])
    s.ev('CompatNative.resetFrameStats("idle")')
    a = threads(pid); t0 = time.monotonic(); time.sleep(5); b = threads(pid)
    idle = diff(a, b, time.monotonic() - t0); idle_fps = s.obj("CompatNative.frameStats()")["fps"]
    s.ev("kag.allskip = 1"); s.ev("SystemAction.skip()")
    p0 = s.obj("llljTestState()")["point"]
    s.ev('CompatNative.resetFrameStats("skip")')
    a = threads(pid); t0 = time.monotonic(); time.sleep(secs); b = threads(pid); dt = time.monotonic() - t0
    st = s.obj("CompatNative.frameStats()"); p1 = s.obj("llljTestState()")["point"]
    affs = set()
    for t in Path(f"/proc/{pid}/task").iterdir():
        try:
            if (t/"comm").read_text().strip() == "DrawThread":
                affs.add([l for l in (t/"status").read_text().splitlines() if l.startswith("Cpus_allowed_list")][0].split()[-1])
        except Exception: pass
    by = diff(a, b, dt)
    game = sum(v for k, v in by.items() if not k.startswith("llvmpipe"))
    lp = sum(v for k, v in by.items() if k.startswith("llvmpipe"))
    top = {k: round(v, 1) for k, v in sorted(by.items(), key=lambda x: -x[1]) if v > 1 and not k.startswith("llvmpipe")}
    r = dict(draw_affinity=sorted(affs), max_interval_ms=round(st["max_interval_ms"],1), idle_cpu=round(sum(idle.values())), idle_fps=round(idle_fps), size=s.ev("kag.innerWidth+\"x\"+kag.innerHeight"), extra=os.environ.get("EXTRA",""), points_per_s=(p1-p0)/dt, fps=st["fps"], game_cpu=round(game), llvmpipe_cpu=round(lp), threads=top,
             compose_ms_per_frame=st["compose_ms"]/st["frames"], upload_ms_per_frame=st["upload_ms"]/st["frames"])
    print(json.dumps(r, ensure_ascii=False))
finally:
    s.close()
