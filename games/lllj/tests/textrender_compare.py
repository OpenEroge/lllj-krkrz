#!/usr/bin/env python3
"""TextRenderBase: the original textrender.dll under Wine vs this port.

Both sides run games/lllj/tests/textrender_probe.tjs, which renders the same strings and
logs each character's position and delay, the key waits and the render bounds.
The original is started like games/lllj/tests/emote_compare.py does (private mirror of the
game directory, script injected through the CialloHook override folder).

Output: OUT/original.txt, OUT/port.txt and a line-by-line diff on stdout.
Requires wine, Xvfb, xdotool.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
GAME = ROOT.parent / "limelight"
from scenarios import Session
from emote_compare import DISPLAY, answer_dialogs, read_text

PROBE = "textrender_probe.tjs"


def wait_done(path, timeout, poll=None):
    import time
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if poll:
            poll()
        text = read_text(path)
        if text.rstrip().endswith("done"):
            return text
        time.sleep(1)
    raise TimeoutError(f"probe did not finish: {path}")


def run_original(out):
    mirror = out / "wine_game"
    mirror.mkdir(parents=True)
    for entry in GAME.iterdir():
        if entry.name in ("savedata", "savedata_zh", "result", "patch", "unencrypted") or entry.suffix in (".exe", ".bak"):
            continue
        (mirror / entry.name).symlink_to(entry)
    shutil.copy2(GAME / "limelight_lj.exe", mirror / "limelight_lj.exe")
    custom = (ROOT / "decomp/text/data/main/custom.tjs").read_text(encoding="utf-8")
    hook = f'\n// LLLJ textrender probe hook\nScripts.execStorage("{PROBE}");\n'
    bom = "\N{ZERO WIDTH NO-BREAK SPACE}"  # krkr reads BOM-less text as Shift_JIS
    for folder in ("unencrypted", "patch"):
        patch = mirror / folder
        patch.mkdir()
        (patch / "custom.tjs").write_text(bom + custom + hook, encoding="utf-8")
        text = (TESTS / PROBE).read_text(encoding="utf-8-sig")
        (patch / PROBE).write_text(bom + text, encoding="utf-8")
    env = dict(os.environ, DISPLAY=DISPLAY, WINEPREFIX=str(out / "wineprefix"), WINEDEBUG="-all",
               WINEDLLOVERRIDES="winmm=n,b;mscoree,mshtml=")
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1920x1200x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    game = None
    try:
        import time
        time.sleep(2)
        subprocess.run(["wineboot", "-i"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        subprocess.run(["wineserver", "-w"], env=env, timeout=300)
        game = subprocess.Popen(["wine", "limelight_lj.exe", "-nod3dm=yes", "-used2d=no"],
                                cwd=mirror, env=env, stdout=(out / "wine.log").open("w"), stderr=subprocess.STDOUT,
                                start_new_session=True)
        return wait_done(mirror / "savedata" / "tr_log.txt", 600, answer_dialogs)
    finally:
        subprocess.run(["wineserver", "-k"], env=env, timeout=30)
        if game and game.poll() is None:
            os.killpg(game.pid, signal.SIGKILL)
        xvfb.terminate()
        xvfb.wait()


def run_port(out):
    session = Session(out / "port_run")
    try:
        session.wait('typeof global.kag == "Object"', 60)
        session.ev(f"Storages.addAutoPath({json.dumps(str(TESTS) + '/')})")
        session.ev(f'Scripts.execStorage("{PROBE}")')
        return wait_done(out / "port_run/saves/tr_log.txt", 300)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--port-only", action="store_true", help="reuse OUT/original.txt")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=args.port_only)
    if not args.port_only:
        (out / "original.txt").write_text(run_original(out), encoding="utf-8")
    port_out = out / "port_run"
    if port_out.exists():
        shutil.rmtree(port_out)
    (out / "port.txt").write_text(run_port(out), encoding="utf-8")
    a = (out / "original.txt").read_text(encoding="utf-8").splitlines()
    b = (out / "port.txt").read_text(encoding="utf-8").splitlines()
    same = sum(1 for x, y in zip(a, b) if x == y)
    print(f"{same}/{max(len(a), len(b))} lines identical")
    for line in difflib.unified_diff(a, b, "original", "port", lineterm="", n=0):
        print(line)


if __name__ == "__main__":
    main()
