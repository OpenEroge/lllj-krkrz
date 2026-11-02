"""Render a fixed set of layer ops and save BMP; compare across thread/chunk settings. usage: pixel_check.py OUT"""
import sys, os, re, json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from scenarios import Session
out = Path(sys.argv[1])
s = Session(out, extra=("-nostartup", *os.environ.get("EXTRA", "").split()))
try:
    s.wait('typeof global.LLLJNative == "Object"', 60)
    img = s.out / "ops.bmp"
    s.ev('global.pxRnd = function() { global.pxSeed = (global.pxSeed * 1103515245 + 12345) & 0x7fffffff; return global.pxSeed; }')
    s.ev('''(function(){
      var w = new Window(); var p = new Layer(w, null); p.setImageSize(64,64);
      var a = new Layer(w, p); a.setImageSize(1920,1080);
      var b = new Layer(w, p); b.setImageSize(1920,1080);
      global.pxSeed = 12345;
      a.fillRect(0,0,1920,1080,0xff204060);
      for (var i=0;i<300;i++) a.fillRect(pxRnd()%1900, pxRnd()%1060, 20+pxRnd()%400, 10+pxRnd()%300, 0xff000000|(pxRnd()&0xffffff));
      b.fillRect(0,0,1920,1080,0x80ff8000);
      for (var i=0;i<200;i++) b.fillRect(pxRnd()%1900, pxRnd()%1060, 20+pxRnd()%600, 10+pxRnd()%400, (pxRnd()&0xff)<<24|(pxRnd()&0xffffff));
      var d = new Layer(w, p); d.setImageSize(1920,1080);
      d.copyRect(0,0,a,0,0,1920,1080);
      d.operateRect(0,0,b,0,0,1920,1080,omAlpha,180);
      d.operateRect(100,50,b,0,0,1700,1000,omAdditive,255);
      d.operateRect(0,0,a,300,200,1200,800,omMultiplicative,200);
      d.operateRect(0,0,b,0,0,1920,1080,omPsScreen,128);
      d.stretchCopy(0,0,960,540,a,0,0,1920,1080,stCubic);
      d.operateStretch(960,540,960,540,b,0,0,1920,1080,omAlpha,200,stLinear);
      d.colorRect(50,50,1800,900,0x336699,100);
      d.adjustGamma(1.2,0,255,1.0,0,255,0.9,0,255);
      d.doBoxBlur(2,2);
      d.saveLayerImage(IMGPATH, "bmp");
      return 1; })()'''.replace("IMGPATH", json.dumps(str(img))))
    import hashlib
    h = hashlib.sha256(img.read_bytes()).hexdigest()[:16]
    log = (out / "game.log").read_text(errors="replace")
    warn = [l for l in log.splitlines() if "drawthreadchunk" in l]
    print(os.environ.get("EXTRA", "") or "(default)", h, *warn, flush=True)
finally:
    s.close()
