#!/usr/bin/env python3
"""E-mote side by side: the original game under Wine vs this port (batch 2).

Both run tools/emote_compare.tjs, which shows the same scenes and captures
them at the same times through KAG's captureSnapshot.

The original runs from a private mirror of the game directory (symlinks plus
a copy of the exe) in OUT, so the game directory stays untouched. The script
is injected through the CialloHook loader shipped with the game: files in
<mirror>/unencrypted/ override game storages, and an overridden main/custom.tjs
ends by executing the capture script. -nod3dm=yes -used2d=no avoid the D3D
motion player and the D2D draw device, which fail under Wine.

Requires wine, Xvfb, ImageMagick (magick/compare). Output: OUT/report.json,
OUT/sheets/<scene>.png (left original, right port), the raw captures.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT.parent / "limelight"
sys.path.insert(0, str(ROOT / "tools"))
from scenarios import Session

DISPLAY = ":79"


def answer_dialogs():
    """First launch asks whether to enable the online patch check; answer No."""
    env = dict(os.environ, DISPLAY=DISPLAY)
    found = subprocess.run(["xdotool", "search", "--name", "ライムライト"], env=env,
                           capture_output=True, text=True).stdout.split()
    for window in found:
        geometry = subprocess.run(["xdotool", "getwindowgeometry", window], env=env,
                                  capture_output=True, text=True).stdout
        size = [line.split()[1] for line in geometry.splitlines() if "Geometry" in line]
        if size and int(size[0].split("x")[0]) < 900:
            subprocess.run(["xdotool", "key", "--window", window, "n"], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_log(path, timeout, poll=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if poll:
            poll()
        if path.exists():
            raw = path.read_bytes()
            text = raw.decode("utf-16", "replace") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
            if " done" in text or " error" in text:
                return text
        time.sleep(1)
    raise TimeoutError(f"capture script did not finish: {path}")


def run_original(out):
    mirror = out / "wine_game"
    mirror.mkdir(parents=True)
    for entry in GAME.iterdir():
        if entry.name in ("savedata", "savedata_zh", "result", "patch", "unencrypted") or entry.suffix in (".exe", ".bak"):
            continue
        (mirror / entry.name).symlink_to(entry)
    shutil.copy2(GAME / "limelight_lj.exe", mirror / "limelight_lj.exe")
    custom = (ROOT / "decomp/text/data/main/custom.tjs").read_text(encoding="utf-8")
    hook = '\n// LLLJ emote comparison hook\nScripts.execStorage("emote_compare.tjs");\n'
    bom = "\N{ZERO WIDTH NO-BREAK SPACE}"  # krkr reads BOM-less text as Shift_JIS
    # CialloHook's loose-file override folder is "unencrypted"; "patch" is
    # also named in the loader, so fill both.
    for folder in ("unencrypted", "patch"):
        patch = mirror / folder
        patch.mkdir()
        (patch / "custom.tjs").write_text(bom + custom + hook, encoding="utf-8")
        for name in ("emote_compare.tjs", "emote_test.ks"):
            text = (ROOT / "tools" / name).read_text(encoding="utf-8-sig")
            (patch / name).write_text(bom + text, encoding="utf-8")
    env = dict(os.environ, DISPLAY=DISPLAY, WINEPREFIX=str(out / "wineprefix"), WINEDEBUG="-all",
               WINEDLLOVERRIDES="winmm=n,b;mscoree,mshtml=")
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1920x1200x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    game = None
    try:
        time.sleep(2)
        # A fresh prefix is still being set up asynchronously on first use;
        # the game's hook loader fails ("Detour Error") if started meanwhile.
        subprocess.run(["wineboot", "-i"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        subprocess.run(["wineserver", "-w"], env=env, timeout=300)
        game = subprocess.Popen(["wine", "limelight_lj.exe", "-nod3dm=yes", "-used2d=no"], cwd=mirror, env=env,
                                stdout=(out / "wine.log").open("w"), stderr=subprocess.STDOUT,
                                start_new_session=True)
        log = wait_log(mirror / "savedata" / "ec_log.txt", 600, answer_dialogs)
    finally:
        subprocess.run(["wineserver", "-k"], env=env, timeout=30)
        if game and game.poll() is None:
            os.killpg(game.pid, signal.SIGKILL)
        xvfb.terminate()
        xvfb.wait()
    dest = out / "original"
    dest.mkdir()
    for f in (mirror / "savedata").glob("ec_*"):
        shutil.copy2(f, dest / f.name)
    return log


def run_port(out, extra):
    session = Session(out / "port_run", extra)
    try:
        session.wait('typeof global.kag == "Object"', 60)
        session.ev(f"Storages.addAutoPath({json.dumps(str(ROOT / 'tools') + '/')})")
        session.ev('Scripts.execStorage("emote_compare.tjs")')
        log = wait_log(out / "port_run/saves/ec_log.txt", 600)
    finally:
        session.close()
    dest = out / "port"
    dest.mkdir()
    for f in (out / "port_run/saves").glob("ec_*"):
        shutil.copy2(f, dest / f.name)
    return log


def to_png(bmp, png):
    data = bmp.read_bytes()
    offset = len(data) - 1920 * 1080 * 4
    if offset in (54, 56) and data[28] == 32:
        # krkrz writes a 54-byte header whose offset/size fields claim 56
        subprocess.run(["magick", "-size", "1920x1080", "-depth", "8", "bgra:-", "-alpha", "off", "-flip", str(png)],
                       input=data[offset:], check=True)
    else:
        subprocess.run(["magick", str(bmp), str(png)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--port-arg", action="append", default=[], help="extra engine argument for the port")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    run_original(out)
    run_port(out, args.port_arg)

    pngs = out / "png"
    sheets = out / "sheets"
    pngs.mkdir()
    sheets.mkdir()
    results = {}
    for bmp in sorted((out / "original").glob("ec_*.bmp")):
        name = bmp.stem[3:]
        port_bmp = out / "port" / bmp.name
        if not port_bmp.exists():
            results[name] = None
            continue
        a, b = pngs / f"original_{name}.png", pngs / f"port_{name}.png"
        to_png(bmp, a)
        to_png(port_bmp, b)
        metric = subprocess.run(["compare", "-metric", "RMSE", str(a), str(b), "null:"],
                                capture_output=True, text=True).stderr
        results[name] = float(metric.split("(")[1].split(")")[0])
    scenes = {}
    for name in results:
        scene, _, at = name.rpartition("_")
        scene = scene if at.isdigit() else name
        scenes.setdefault(scene, []).append(name)
    for scene, names in scenes.items():
        names.sort(key=lambda n: int(n.rpartition("_")[2]) if n.rpartition("_")[2].isdigit() else 0)
        files = [str(pngs / f"{side}_{n}.png") for n in names for side in ("original", "port") if results[n] is not None]
        subprocess.run(["magick", "montage", *files, "-resize", "480x270", "-tile", "2x", "-geometry", "+3+3",
                        "-background", "gray30", str(sheets / f"{scene}.png")], check=True)
    missing = [n for n, v in results.items() if v is None]
    (out / "report.json").write_text(json.dumps({"rmse": results, "missing_in_port": missing}, indent=2))
    print(json.dumps(results, indent=1))
    if missing:
        sys.exit(f"port did not capture: {missing}")


if __name__ == "__main__":
    main()
