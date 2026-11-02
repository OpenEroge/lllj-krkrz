import sys, time, os, subprocess, json, re, collections
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from scenarios import Session, advance
out = Path(sys.argv[1]); n = int(sys.argv[2])
s = Session(out, extra=tuple(os.environ.get("EXTRA","").split()))
try:
    s.boot(); s.finish_startup(); time.sleep(2)
    s.ev("kag.setInnerSize(1920,1080)")
    s.jump("title.ks", "*game", "game")
    s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(s, 2); time.sleep(1)
    pid = int(subprocess.check_output(["pgrep", "-n", "-f", f"datapath={s.out}/saves/"]).split()[0])
    s.ev("kag.allskip = 1"); s.ev("SystemAction.skip()"); time.sleep(0.5)
    stacks = []
    for i in range(n):
        o = subprocess.run(["gdb", "-p", str(pid), "-batch", "-ex", "set pagination off", "-ex", "thread 1", "-ex", "bt 40"], capture_output=True, text=True).stdout
        frames = re.findall(r"^#\d+\s+(?:0x[0-9a-f]+ in )?(\S+)", o, re.M)
        stacks.append(frames)
        time.sleep(0.15)
    (out / "stacks.json").write_text(json.dumps(stacks))
    print("points", s.obj("llljTestState()")["point"])
finally:
    s.close()
