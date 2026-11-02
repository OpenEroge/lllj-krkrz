import sys, time, os, subprocess, json
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from scenarios import Session, advance
out = Path(sys.argv[1]); secs = 10
HZ = os.sysconf("SC_CLK_TCK")
def snap(pid):
    r = {}
    for t in Path(f"/proc/{pid}/task").iterdir():
        try:
            st = (t/"stat").read_text(); name = st[st.index("(")+1:st.rindex(")")]
            f = st[st.rindex(")")+2:].split()
            sw = dict(l.split(":\t") for l in (t/"status").read_text().splitlines() if "ctxt" in l)
            ss = (t/"schedstat").read_text().split()
            r[t.name] = (name, int(f[11]), int(f[12]), int(sw["voluntary_ctxt_switches"]), int(sw["nonvoluntary_ctxt_switches"]), int(ss[0]))
        except Exception: pass
    return r
s = Session(out, extra=tuple(os.environ.get("EXTRA","").split()))
try:
    s.boot(); s.finish_startup(); time.sleep(2)
    s.ev("kag.setInnerSize(1568,882)")
    s.jump("title.ks", "*game", "game")
    s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(s, 2); time.sleep(1)
    pid = int(subprocess.check_output(["pgrep", "-n", "-f", f"datapath={s.out}/saves/"]).split()[0])
    s.ev("kag.allskip = 1"); s.ev("SystemAction.skip()"); time.sleep(0.5)
    s.ev('LLLJNative.resetFrameStats("skip")')
    a = snap(pid); time.sleep(secs); b = snap(pid)
    st = s.obj("LLLJNative.frameStats()")
    agg = {}
    for tid, v in b.items():
        if v[0].startswith("llvmpipe"): continue
        u = a.get(tid, (v[0], 0, 0, 0, 0, 0))
        g = agg.setdefault(v[0], [0, 0, 0, 0, 0, 0])
        g[0] += 1; g[1] += (v[1]-u[1]); g[2] += (v[2]-u[2]); g[3] += v[3]-u[3]; g[4] += v[4]-u[4]; g[5] += v[5]-u[5]
    print("EXTRA", os.environ.get("EXTRA",""), "fps", round(st["fps"]), "compose ms/frame", round(st["compose_ms"]/st["frames"], 2))
    for tid, v in sorted(b.items(), key=lambda x: int(x[0])):
        if v[0] != "DrawThread": continue
        u = a.get(tid); aff = [l for l in Path(f"/proc/{pid}/task/{tid}/status").read_text().splitlines() if l.startswith("Cpus_allowed_list")][0].split()[-1]
        vol = v[3]-u[3]; ns = v[5]-u[5]
        print(f"    cpu={aff:>5s} wakeups/s={vol/secs:6.0f} cpu={ns/1e7/secs:5.1f}% per_wakeup={ns/max(vol,1)/1000:6.1f}us")
    for k in ("krkrz", "DrawThread"):
        n, ut, stt, vol, inv, ns = agg.get(k, [0]*6)
        print(f"  {k:11s} threads={n:2d} user={100*ut/HZ/secs:5.1f}% sys={100*stt/HZ/secs:5.1f}% wakeups/s={vol/secs:8.0f} preempt/s={inv/secs:6.0f} schedstat={ns/1e7/secs:5.1f}% per_wakeup={ns/max(vol,1)/1000:6.1f}us")
finally:
    s.close()
