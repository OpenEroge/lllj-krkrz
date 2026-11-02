import sys, time, os, subprocess, json, re
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # games/lllj/tests
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../tools/dev")))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../..")))
from scenarios import Session, advance
out = Path(sys.argv[1]); n = int(sys.argv[2])
s = Session(out, extra=tuple(os.environ.get("EXTRA","").split()))
try:
    s.boot(); s.finish_startup(); time.sleep(2)
    s.ev("kag.setInnerSize(1568,882)")
    s.jump("title.ks", "*game", "game")
    s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(s, 2); time.sleep(1)
    pid = int(subprocess.check_output(["pgrep", "-n", "-f", f"datapath={s.out}/saves/"]).split()[0])
    s.ev("kag.allskip = 1"); s.ev("SystemAction.skip()"); time.sleep(0.5)
    dumps = []
    for i in range(n):
        dumps.append(subprocess.run(["gdb", "-p", str(pid), "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt 12"], capture_output=True, text=True).stdout)
        time.sleep(0.2)
    (out / "dumps.json").write_text(json.dumps(dumps))
finally:
    s.close()
