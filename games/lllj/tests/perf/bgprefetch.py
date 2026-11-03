#!/usr/bin/env python3
"""Check the background prefetch: how many background loads find their image already decoded.

  python3 -I games/lllj/tests/perf/bgprefetch.py OUT [SECONDS] [--no-prefetch]

Boots, starts the story and fast-forwards (kag.allskip) for SECONDS (default 30).
The game calls touchImage(name) right before every AffineSourceImage load; this
hooks it (setTouchStore) and records, for each PNG/JPEG background, whether the
graphic cache already held it ("cached"), whether a prefetch was still decoding
("inflight", the load then waits for it), or neither ("miss", decoded on the
main thread). It also times a single prefetched load from the REPL.
--no-prefetch runs with -lllj_prefetch=no for comparison.
env: EXTRA="..." extra engine options.
"""
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # games/lllj/tests
from scenarios import Session, advance

PROBE_TJS = r"""
global.llljBgLog = [];
function llljBgCached(name) {
	var leaf = Storages.extractStorageName(name).toLowerCase();
	var list = Storages.getImageCacheList();
	for (var i = 0; i < list.count; i++)
		if (Storages.extractStorageName(list[i].path).toLowerCase() == leaf) return true;
	return false;
}
setTouchStore(function(name) {
	if (typeof name != "String") return;
	var ext = Storages.extractStorageExt(name).toLowerCase();
	if (ext != ".png" && ext != ".jpg") return;
	var state = llljBgCached(name) ? "cached" : CompatNative.imagePrefetchBusy() ? "inflight" : "miss";
	llljBgLog.add(%[ name:name, state:state ]);
});
function llljPrefetchTiming(name) {
	System.clearGraphicCache();
	CompatNative.prefetchImage(name);
	var t0 = System.getTickCount();
	while (CompatNative.imagePrefetchBusy() && System.getTickCount() - t0 < 2000) ;
	var decoded = System.getTickCount() - t0;
	var l = new Layer(kag, kag.fore.base);
	var t1 = System.getTickCount();
	l.loadImages(name);
	var load = System.getTickCount() - t1;
	invalidate l;
	return %[ worker_ms:decoded, load_ms:load ];
}
"""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = Path(args[0])
    seconds = float(args[1]) if len(args) > 1 else 30
    off = "--no-prefetch" in sys.argv
    extra = (("-lllj_prefetch=no",) if off else ()) + tuple(os.environ.get("EXTRA", "").split())
    s = Session(out, extra=extra)
    try:
        s.boot()
        s.finish_startup()
        time.sleep(1)
        script = s.out / "bgprefetch.tjs"
        script.write_text(PROBE_TJS)
        s.ev(f"Scripts.execStorage({json.dumps(str(script))})")
        timing = s.obj('llljPrefetchTiming("学園_教室a_春.png")')
        s.jump("title.ks", "*game", "game")
        s.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
        advance(s, 2)
        s.ev("kag.allskip = 1")
        s.ev("SystemAction.skip()")
        time.sleep(seconds)
        log = s.obj("llljBgLog")
        states = {k: sum(1 for e in log if e["state"] == k) for k in ("cached", "inflight", "miss")}
        r = dict(prefetch=not off, seconds=seconds, loads=len(log), **states,
                 single_prefetch=timing, point=s.obj("llljTestState()").get("point"))
        (out / "bgprefetch.json").write_text(json.dumps(dict(r, log=log), ensure_ascii=False, indent=2))
        print(json.dumps(r, ensure_ascii=False))
    finally:
        s.close()


if __name__ == "__main__":
    main()
