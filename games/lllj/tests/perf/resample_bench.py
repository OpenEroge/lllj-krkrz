"""Resample (Layer.stretchCopy stLinear/stCubic) wall time under different thread settings. usage: resample_bench.py OUT"""
import sys, time, os
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools/dev"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scenarios import Session
out = Path(sys.argv[1])
s = Session(out, extra=("-nostartup", *os.environ.get("EXTRA", "").split()))
try:
    s.wait('typeof global.CompatNative == "Object"', 60)
    s.ev('''(function(){ global.rb = %[]; rb.w = new Window(); rb.p = new Layer(rb.w, null); rb.p.setImageSize(64,64);
      rb.src = new Layer(rb.w, rb.p); rb.src.setImageSize(1920,1080); rb.src.fillRect(0,0,1920,1080,0xff336699);
      for (var i=0;i<200;i++) rb.src.fillRect(Math.random()*1800, Math.random()*1000, 100, 60, 0xff000000 | (int)(Math.random()*0xffffff));
      rb.dst = new Layer(rb.w, rb.p); rb.dst.setImageSize(2560,1440); return 1; })()''')
    res = {}
    for name in ("stLinear", "stCubic"):
        s.ev(f"rb.dst.stretchCopy(0,0,2560,1440, rb.src,0,0,1920,1080, {name})")  # warm up
        best = None
        for rep in range(3):
            ms = float(s.ev(f"(function(){{ var t=System.getTickCount(); for(var i=0;i<20;i++) rb.dst.stretchCopy(0,0,2560,1440, rb.src,0,0,1920,1080, {name}); return ((string)((System.getTickCount()-t)/20)); }})()").strip('"'))
            best = ms if best is None else min(best, ms)
        res[name] = best
    print(os.environ.get("EXTRA", "") or "default(20)", " ".join(f"{k}={v:.1f}ms" for k, v in res.items()), flush=True)
finally:
    s.close()
