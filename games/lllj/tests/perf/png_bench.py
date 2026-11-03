#!/usr/bin/env python3
"""Time background PNG loads on the main thread (decode, no cache).

  python3 -I games/lllj/tests/perf/png_bench.py OUT [COUNT]

Boots to the title, then loads COUNT backgrounds from game/bgimage with
Layer.loadImages, clearing the graphic cache before every load. Each image is
loaded three times and the fastest is kept (System.getTickCount has 1 ms
resolution). Runs with -lllj_zh=no so every name is a real PNG.
env: EXTRA="..." extra engine options.
"""
import json
import os
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # games/lllj/tests
from scenarios import ROOT, Session

BENCH_TJS = r"""
function llljPngBench(names, reps) {
	var l = new Layer(kag, kag.fore.base);
	var res = [];
	for (var i = 0; i < names.count; i++) {
		var best = 1000000;
		for (var r = 0; r < reps; r++) {
			System.clearGraphicCache();
			var t = System.getTickCount();
			l.loadImages(names[i]);
			var d = System.getTickCount() - t;
			if (d < best) best = d;
		}
		res.add(best);
	}
	invalidate l;
	return res;
}
"""


def main():
    out = Path(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    pngs = sorted(p.name for p in (ROOT / "game/bgimage").iterdir() if p.suffix.lower() == ".png")
    step = max(1, len(pngs) // count)
    names = pngs[::step][:count]
    s = Session(out, extra=("-lllj_zh=no", *os.environ.get("EXTRA", "").split()))
    try:
        s.boot()
        s.finish_startup()
        time.sleep(2)
        script = s.out / "png_bench.tjs"
        script.write_text(BENCH_TJS)
        s.ev(f"Scripts.execStorage({json.dumps(str(script))})")
        ms = json.loads(json.loads(s.ev(
            f"Scripts.toJSONString(llljPngBench({json.dumps(names, ensure_ascii=False)}, 3))", timeout=120)))
        r = dict(images=len(names), median_ms=statistics.median(ms), mean_ms=round(statistics.mean(ms), 2),
                 max_ms=max(ms), min_ms=min(ms), extra=os.environ.get("EXTRA", ""),
                 per_image=dict(zip(names, ms)))
        (out / "png_bench.json").write_text(json.dumps(r, ensure_ascii=False, indent=2))
        print(json.dumps({k: v for k, v in r.items() if k != "per_image"}))
    finally:
        s.close()


if __name__ == "__main__":
    main()
