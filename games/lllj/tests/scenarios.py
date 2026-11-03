#!/usr/bin/env python3
"""Game session driver and the scenario case bodies (run through games/lllj/tests/run.py)."""
import json
import re
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
from repl import evaluate
from import_saves import import_saves


class Session:
    def __init__(self, out, extra=(), gallery_seed=False, save_seed=None, headless=True, logo=True):
        # logo=False starts at the title (-lllj_logo=no): no caution screen or brand logos.
        self.out = Path(out).resolve()
        self.logo = logo
        self.out.mkdir(parents=True, exist_ok=True)
        self.headless = headless
        self.channel = self.out / "repl"
        if self.channel.exists():
            raise RuntimeError(f"Choose a fresh output directory: {self.channel}")
        if gallery_seed:
            save_dir = self.out / "saves"
            save_dir.mkdir()
            seed = ROOT.parent / "limelight/补丁/全CG存档/全CG存档.zip"
            with zipfile.ZipFile(seed) as archive:
                (save_dir / "datasu.ksd").write_bytes(archive.read("datasu.ksd"))
        if save_seed:
            import_saves(save_seed, self.out / "saves")
        self.log = (self.out / "game.log").open("w")
        (self.out / "tmp").mkdir()
        env = os.environ.copy()
        env.update(HEADLESS="1", REPLCH=str(self.channel), XDG_DATA_HOME=str(self.out / "xdg"), TMPDIR=str(self.out / "tmp"))
        if not headless:
            env.pop("HEADLESS", None)
            env["SDL_AUDIO_DRIVER"] = "dummy"
        self.proc = subprocess.Popen(
            [str(ROOT / "run.sh"), f"-datapath={self.out}/saves/", "-fpsstats=yes", "-language=en",
             *([] if logo else ["-lllj_logo=no"]), *extra],
            cwd=ROOT, env=env, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        self.steps = []
        self.started = time.monotonic()

    def click(self, x, y, button=1):
        env = os.environ.copy()
        env["REPLCH"] = str(self.channel)
        subprocess.run([str(ROOT / "tools/dev/click.sh"), str(x), str(y), str(button)], env=env, check=True, stdout=subprocess.DEVNULL)

    def key(self, key):
        env = os.environ.copy()
        env["REPLCH"] = str(self.channel)
        subprocess.run([str(ROOT / "tools/dev/click.sh"), "key", key], env=env, check=True, stdout=subprocess.DEVNULL)

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate() # launcher stops game before its X server
            try:
                # An instrumented (PGO) build writes its profile at exit and needs longer.
                self.proc.wait(timeout=float(os.environ.get("KRKRZ_EXIT_TIMEOUT", "5")))
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait()
        self.log.close()

    def ev(self, expression, timeout=15):
        if self.proc.poll() is not None:
            raise RuntimeError(f"Game exited ({self.proc.returncode}); see {self.out}/game.log")
        return evaluate(self.channel, expression, timeout)["result"]

    def obj(self, expression):
        return json.loads(json.loads(self.ev(f"Scripts.toJSONString({expression})")))

    def wait(self, expression, timeout=20):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"Game exited ({self.proc.returncode}); see game.log")
            # run.sh clears stale requests from the channel and then writes pid: a request
            # sent before that could be deleted unanswered
            if (self.channel / "pid").exists():
                last = self.ev(f"!!({expression})")
                if last == "1":
                    return
            time.sleep(0.02)
        raise TimeoutError(f"Condition timed out: {expression} (last: {last})")

    def boot(self):
        if self.logo:  # at the caution screen
            self.wait('typeof global.kag == "Object" && typeof global.SystemHook == "Object" && kag.conductor.curStorage == "custom.ks"', 60)
        else:
            self.wait('typeof global.kag == "Object" && typeof global.SystemHook == "Object"', 60)
        self.ev(f"Scripts.execStorage({json.dumps(str(TESTS / 'scenarios.tjs'))})")

    def start(self):
        """Fast start for the test cases: boot straight to the title (logo=False) and click
        through its intro motion, as a player can."""
        self.boot()
        self.wait('SystemHook.currentState == "title"', 60)
        self.click_until('SystemHook.currentState == "title" && kag.inStable')

    def click_until(self, expression, timeout=30):
        """Primary clicks (KAG click-to-skip of transitions and waits) until expression holds."""
        deadline = time.monotonic() + timeout
        while self.ev(f"!!({expression})") != "1":
            if time.monotonic() > deadline:
                raise TimeoutError(f"Condition timed out while clicking: {expression}")
            self.ev("kag.onPrimaryClick()")
            time.sleep(0.05)

    def finish_startup(self):
        # Let caution/Logo cleanup run: jumping out early leaves its transparent
        # SysCoverLayer above the title buttons and creates a false input bug.
        self.ev('CompatNative.resetFrameStats("startup_logos")')
        if self.ev('SystemHook.currentState != "title"') == "1":
            if self.headless:
                self.click(720, 400) # normal click-to-skip caution path
            else:
                self.ev("kag.onPrimaryClick()")
        self.wait('SystemHook.currentState == "title" && kag.inStable', 90)
        stats = self.obj("CompatNative.frameStats()")
        self.steps.append({"name": "startup_logos", "state": self.obj("llljTestState()"),
                           "stats": stats, "screenshot_size": self.capture("00_startup_finished"),
                           "note": "Normal caution/Logo traversal."})
        self.write_report()

    def jump(self, storage, label, mode=None):
        self.ev(f"llljTestJump({json.dumps(storage)},{json.dumps(label)})")
        if mode:
            self.wait(f"SystemHook.currentState == {json.dumps(mode)} && kag.inStable")

    def capture(self, name, ext="png", before=None):
        # .bmp is saved uncompressed (frame comparisons). before: TJS run first, in the same call.
        path = self.out / f"{name}.{ext}"
        self.ev((f"{before}," if before else "") + f"System.captureScreen({json.dumps(str(path))})")
        return self.captured(path)

    def captured(self, path):
        """Wait for a requested screenshot (saved with the next frame); its [width, height]."""
        ext = path.suffix[1:]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size > 24:
                head = path.read_bytes()[:26]
                if ext == "bmp" and len(head) >= 26:
                    if head[:2] != b"BM":
                        raise AssertionError(f"Invalid screenshot: {path}")
                    return [abs(v) for v in struct.unpack("<ii", head[18:26])]
                if ext == "png" and head[:8] != b"\x89PNG\r\n\x1a\n":
                    raise AssertionError(f"Invalid screenshot: {path}")
                if ext == "png":
                    return list(struct.unpack(">II", head[16:24]))
            time.sleep(0.01)
        raise TimeoutError(f"Screenshot missing: {path}")

    def step(self, name, seconds=0.2):
        """Presentation stats over seconds, the game state and a screenshot. One REPL request
        for all of it: the engine answers one request per frame."""
        self.ev(f"CompatNative.resetFrameStats({json.dumps(name)})")
        time.sleep(seconds)
        try:  # with a low -maxfps the window may hold no present yet
            self.wait("CompatNative.frameStats().frames > 0", 1)
        except TimeoutError:
            pass  # asserted below
        path = self.out / f"{name}.png"
        data = self.obj("(function(){ var r = %[stats: CompatNative.frameStats(), state: llljTestState(), "
                        "storage: CompatNative.storageStats()]; System.captureScreen(" + json.dumps(str(path)) + "); return r; })()")
        stats, state = data["stats"], data["state"]
        assert stats["detailed"] == 1, "Build with KRKRZ_DRAW_STATS=ON"
        assert stats["frames"] > 0, f"No presentations in {name}"
        dimensions = self.captured(path)
        self.steps.append({"name": name, "t": round(time.monotonic() - self.started, 2), "state": state,
                           "stats": stats, "screenshot_size": dimensions, "storage_stats": data["storage"]})
        self.write_report()
        print(f"{name}: {state['mode']} {state['storage']} ({stats['fps']:.1f} present/s)", flush=True)
        return state

    def write_report(self, **extra):
        import re
        text = (self.out / "game.log").read_text(errors="replace")
        renderers = re.findall(r"\(compat\) renderer: (.*)", text)
        report = {"steps": self.steps, "environment": ("Xvfb" if self.headless else "desktop") + "; completed swaps, not scanout refresh", "renderers": renderers, **extra}
        (self.out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))


# System members the game calls that have no Win32 counterpart on this port. modules/win32
# stubs them (compat_stub) and each returns the value the game takes when the facility is
# absent. Any other "(compat) stub:" line fails a run, so a new stub must be evaluated here.
WIN32_STUBS = {
    "System.setDpiAwareness": "mainwindow.tjs requests per-monitor DPI awareness at startup; SDL scales the window itself",
    "System.readRegValue": "utils.tjs (getSystemMouseCursor, expandEnvironmentString) and speech_dlg read the Windows registry; no value selects the defaults",
    "System.setIconicPreview": "mainwindow.tjs sets the Windows taskbar thumbnail preview; Linux desktops have no such surface",
}


# Exceptions the game scripts throw and catch themselves (krkrz logs them at the throw site,
# the original engine does not), keyed by (script, function, VM ip). Each needs a reason.
KNOWN_CAUGHT = {
    ("multilang.vanilla.tjs", "get", "3"): "Chinese patch: snapcapture.tjs fills SystemConfig defaults at load and so reads the "
        "SaveSnapshotWatermark image proxy before world_object exists; MakeImageDataProxy.get catches that and keeps the name",
    ("anonymous", "uiLanguageType", "5"): "Chinese patch: its SystemConfig.uiLanguageType getter is read while kag is being "
        "constructed; the getter wraps global.kag in try/catch and returns 0",
    ("affinesourcemotion.tjs", "global", "27"): "E-mote: the script reads Motion.Player.useD3D in try/catch to choose the D3D "
        "motion path; motionplayer_nod3d.dll (and this port) has no such member, so Motion.enableD3D becomes false",
}
# Information dialogs that are part of the game: the Chinese patch's anti-resale notice at every start.
KNOWN_DIALOGS = ("本补丁为免费分享、无任何要求即可获取，谨防上当受骗；",)


def stubs_seen(path):
    import re
    return sorted(set(re.findall(r"\(compat\) stub: (\S+)", Path(path).read_text(errors="replace"))))


def check_log(path):
    # Exact call sites only. All unmatched exceptions remain failures.
    import re
    text = Path(path).read_text(errors="replace")
    matches = [(re.sub(r"@0x[0-9a-f]+$", "", m[1]), m[2], m[3]) for m in re.finditer(
        r"exception occured at ([^(]+)\(\d+\)\[\((?:function|property getter|property setter|top level script)\) ([^\]]+)\], VM ip = (\d+)", text)]
    known = sorted({m for m in matches if m in KNOWN_CAUGHT})
    unknown = [m for m in matches if m not in KNOWN_CAUGHT]
    # Also reject fatal errors and exception headers the parser did not recognize.
    headers = text.count("exception occured")
    if "source unsupported" in text:  # E-mote content the runtime cannot draw
        raise AssertionError("E-mote reported an unsupported source; see game.log")
    if "msdfrender not linked" in text:  # PreRenderFontEx would fall back to plain glyphs
        raise AssertionError("PreRenderFontEx reported msdfrender not linked; see game.log")
    unexpected = [name for name in stubs_seen(path) if name not in WIN32_STUBS]
    if unexpected:
        raise AssertionError(f"Unevaluated prelude stubs were called: {unexpected}; see game.log")
    dialogs = [line for line in re.findall(r"\[dialog\] Information: (.*)", text) if not line.startswith(KNOWN_DIALOGS)]
    if headers != len(matches) or unknown or dialogs or "prelude error:" in text:
        raise AssertionError(f"Unhandled/unexpected game errors: {unknown}; headers={headers}, parsed={len(matches)}")
    return known




def advance(session, lines):
    """Click until the dialogue cursor moves, once per line. A click that only completes
    the text display leaves the cursor in place, so it is clicked again after a short wait."""
    for _ in range(lines):
        before = session.obj("llljTestState()")
        for attempt in range(12):
            session.ev("kag.onPrimaryClick()")
            deadline = time.monotonic() + 0.3
            while time.monotonic() < deadline:
                time.sleep(0.02)
                after = session.obj("llljTestState()")
                if (after.get("scene"), after.get("cursor")) != (before.get("scene"), before.get("cursor")):
                    break
            else:
                continue
            break
        else:
            raise AssertionError(f"Dialogue failed to advance: {before}")
    session.wait("kag.inStable")


def frames_differ(session, prefix, count, interval, need=None, start=None):
    """Capture up to count frames interval seconds apart (uncompressed); return how many
    distinct images were seen, stopping early once need distinct ones have been.
    start: TJS run in the same call as the first capture request, so that capture is the
    first frame drawn after it (a motion that changes once and holds cannot be missed)."""
    import hashlib
    digests = set()
    for k in range(count):
        name = f"{prefix}_{k:02d}"
        if k == 0 and start:
            session.capture(name, "bmp", start)
        else:
            time.sleep(interval)
            session.capture(name, "bmp")
        path = session.out / f"{name}.bmp"
        deadline = time.monotonic() + 5
        while True:  # the engine writes the file in place: wait until all pixels are there
            data = path.read_bytes()
            if len(data) >= 30:  # 14-byte file header + info header + 32-bit rows
                info, w, h, _, bpp = struct.unpack("<Iii HH", data[14:30])
                if len(data) >= 14 + info + w * abs(h) * bpp // 8:
                    break
            assert time.monotonic() < deadline, f"Screenshot incomplete: {path}"
            time.sleep(0.01)
        digests.add(hashlib.sha256(data).hexdigest())
        path.unlink()
        if need and len(digests) >= need:
            break
    return len(digests)


def enter_game(session, lines=0):
    """New game from the title, clicking through the opening transitions to the first line."""
    session.ev('llljTestJump("title.ks","*game")')
    session.click_until('SystemHook.currentState == "game" && typeof world_object.player == "Object" && '
                        'world_object.player.curPoint > 0 && kag.inStable', 60)
    if lines:
        advance(session, lines)


def back_to_game(session, storage):
    session.jump(storage, "*back", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0')
    session.wait("kag.inStable")


# ---------------------------------------------------------------------------------------
# Cases. Each runs in its own engine instance (games/lllj/tests/cases.py), starting at a
# stable title unless noted, and ends with check_log on its own game.log.

def startup(session):
    """The real start: caution screen, the two E-mote brand logos, then the title (logo=True)."""
    session.boot()
    session.step("01_attention")
    session.ev('CompatNative.resetFrameStats("startup_logos")')
    session.click(720, 400)  # normal click-to-skip caution path; the logos are E-mote motions
    logos = frames_differ(session, "08_logo", 16, 0.25, need=4)
    assert logos >= 4, f"Logo motions look static ({logos} distinct frames)"
    # Let caution/Logo cleanup run: jumping out early leaves its transparent
    # SysCoverLayer above the title buttons and creates a false input bug.
    session.wait('SystemHook.currentState == "title" && kag.inStable', 90)
    session.steps.append({"name": "startup_logos", "state": session.obj("llljTestState()"),
                          "stats": session.obj("CompatNative.frameStats()"), "distinct_logo_frames": logos,
                          "screenshot_size": session.capture("00_startup_finished")})
    assert session.ev("typeof global.Motion") == '"Object"'
    session.step("02_title", 1)


def dialogue_backlog(session):
    enter_game(session, 8)
    session.step("03_dialogue", 1)
    session.ev("SystemAction.backlog()")
    session.wait('SystemHook.currentState == "backlog"')
    session.step("04_backlog")
    back_to_game(session, "backlog.ks")


def save_load_roundtrip(session, kinds=("normal", "quick")):
    """Real serialized game round trips (including a persistent game flag), rather than
    merely checking that a file exists."""
    indexes = {"normal": "SystemConfig.savedataTopIndex", "quick": "SystemAction.qsaveBookmarkNumber"}
    for name in kinds:
        index = indexes[name]
        session.ev('f.llljSmokeMarker=12345,kag.storeFlags()')
        before = session.obj("llljTestState()")
        assert session.ev(f"kag.saveBookMark({index})") == "1"
        filename = json.loads(session.ev(f"kag.getBookMarkFileNameAtNum({index})"))
        session.ev("f.llljSmokeMarker=54321")
        advance(session, 3)
        session.ev(f"kag.loadBookMark({index})")
        session.wait("f.llljSmokeMarker == 12345")
        after = session.step(f"06_{name}_loaded")
        assert before["scene"] and after["mode"] == "game", (before, after)
        assert after["scene"] == before["scene"] and after["point"] == before["point"], (before, after)
        assert session.ev(f"typeof Scripts.loadDataPack({json.dumps(filename)})") == '"Object"'


def save_pages(session):
    """The save and load pages from the game."""
    enter_game(session, 2)
    session.ev("SystemAction.save()")
    session.wait('SystemHook.currentState == "save"')
    session.step("05_save_page")
    back_to_game(session, "save.ks")
    session.ev("SystemAction.load()")
    session.wait('SystemHook.currentState == "load"')
    session.step("05_load_page")
    back_to_game(session, "load.ks")


def save_load(session):
    """Normal and quick save round trips."""
    enter_game(session, 2)
    save_load_roundtrip(session)


def jp(session):
    """The Japanese-only game (-lllj_zh=no): dialogue, backlog and a save round trip."""
    assert session.ev("CompatNative.xpckEnabled()") == "0", "lllj_zh.pck is mounted"
    session.step("jp_01_title")
    enter_game(session, 4)
    session.step("jp_02_dialogue")
    session.ev("SystemAction.backlog()")
    session.wait('SystemHook.currentState == "backlog"')
    session.step("jp_03_backlog")
    back_to_game(session, "backlog.ks")
    save_load_roundtrip(session, ("normal",))


def gallery(session, modes):
    for mode in modes:
        if mode == "exchview":
            session.jump("custom.ks", "*exchview", mode)
        else:
            session.jump("title.ks", f"*{mode}", mode)
        session.step(f"07_{mode}")
        if mode == "cgmode":
            for prefix, label in (("ev152", "uncensored_cg"), ("sd101", "sd_gallery")):
                index = int(session.ev(f"llljTestGalleryFind({json.dumps(prefix)})"))
                assert index >= 0, f"Gallery item missing: {prefix}"
                session.ev(f'Current.func("onSelect")({index})')
                session.jump("cgmode.ks", "*view", "cgmode")
                session.step(f"07_{label}")
                session.jump("cgmode.ks", "*view_rclick", "cgmode")
                session.wait('Current.hasDefined("items")')
            assert session.ev('typeof global.SoundModeInstance') == '"Object"'
            session.ev('global.llljTestMusic=SoundModeInstance.items[0].storage,SoundModeInstance.play(llljTestMusic)')
            session.wait('SoundModeInstance.playing == llljTestMusic')
            session.step("07_music_player")
            session.ev("SoundModeInstance.stop()")
        session.jump(f"{mode}.ks", "*back", "title")


def settings(session, first, last):
    """Option pages first..last (of 10), then back to the title."""
    session.jump("title.ks", "*option", "option")
    pages = session.obj('Current.propget("pagetabs")')
    assert len(pages) == 10, pages
    for i, page in list(enumerate(pages))[first:last + 1]:
        session.ev(f'Current.page={i},kag.process("option.ks","*page")')
        session.wait(f'Current.propget("page") == {i} && kag.inStable')
        session.step(f"08_option_{page}")
    session.jump("option.ks", "*back", "title")
    session.step("09_return_title")


def controls(session):
    initial = session.obj("llljTestState()")
    session.key("alt+Return")
    session.wait(f'kag.fullScreen != {int(bool(initial["fullscreen"]))}')
    session.step("10_fullscreen")
    session.key("alt+Return")
    session.wait(f'kag.fullScreen == {int(bool(initial["fullscreen"]))}')
    session.ev("kag.setInnerSize(1280,720)")
    session.wait("kag.innerWidth == 1280 && kag.inStable")
    time.sleep(0.3)  # the X window follows the inner size asynchronously
    session.step("11_resized")
    # Native pointer routing after resizing, with the all-CG fixture's complete
    # title menu. Its first button center is (144,1028) at 1920x1080.
    session.click(96, 685)
    session.wait('SystemHook.currentState == "game"')
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    session.wait("kag.inStable", 30)
    session.ev("SystemAction.option()")
    session.wait('SystemHook.currentState == "option" && kag.inStable')
    session.click(600, 450, 3)
    session.wait('SystemHook.currentState == "game"')
    session.step("12_right_click_return")
    session.wait("kag.inStable")
    before = session.obj("llljTestState()")
    for _ in range(8):
        session.key("Return")
        time.sleep(0.25)
        after = session.obj("llljTestState()")
        if (after.get("scene"), after.get("cursor")) != (before.get("scene"), before.get("cursor")):
            break
    else:
        raise AssertionError("Normal key forwarding failed after Alt+Enter wrapper")
    session.step("13_keyboard_advance")



EMOTE_EVS = {
    "sd001a01": "sd001.mtn",
    "sd002_ea01_ta01": "sd002.mtn",
    "sd003a02": "sd003.mtn",
    "sd004_eb01": "sd004.mtn",
    "sd005a_e01": "sd005.mtn",
    "sd006a_ta01": "sd006.mtn",
    "sd007a": "sd007.mtn",
    "sd008a": "sd008.mtn",
    "sd009_1": "sd009.mtn",
    "sd010_t01a": "sd010.mtn",
    "sd011a_t01a": "sd011.mtn",
    "sd012a_ea01": "sd012.mtn",
    "sd013_ea01_ya01": "sd013.mtn",
    "sd014a_aa01": "sd014.mtn",
    "sd015_a01_t01_n01": "sd015.mtn",
    "sd016_e02": "sd016.mtn",
    "sd101a_e01_r01": "sd101.mtn",
    "sd102a_e01": "sd102.mtn",
    "sd103a_e02": "sd103.mtn",
    "sd104a_e01_y01": "sd104.mtn",
    "sd105a_ea03_r02": "sd105.mtn",
    "sd201a": "sd201.mtn",
    "sd202_01": "sd202.mtn",
    "sd301_h01_s01_y01": "sd301.mtn",
    "sd302a_t01_r01": "sd302.mtn",
    "sd401ab_r09_yc03": "sd401.mtn",
    "sd402_ra06": "sd402.mtn",
    "sd403_ra01": "sd403.mtn",
    "sd404_y01_m01": "sd404.mtn",
    "sd501a_ra02": "sd501.mtn",
    "sd601_ra01": "sd601.mtn",
    "title_bg0": "title_bg.mtn",
    "title_bg1": "title_bg1.mtn",
    "title_bg2": "title_bg2.mtn",
    "title_bg3": "title_bg3.mtn",
    "title_bg4": "title_bg4.mtn",
    "title_bg5": "title_bg5.mtn",
    "title_bg6": "title_bg6.mtn",
    "title_bg_trial": "title_bg_trial.mtn",
}




def emote_motions(session, names):
    """E-mote: each listed [ev] motion loads, draws and animates."""
    enter_game(session)
    session.ev(f"Storages.addAutoPath({json.dumps(str(TESTS) + '/')})")
    for name in names:
        session.ev(f"tf.llljEv={json.dumps(name)}")
        # some motions hold still for ~2.5s, others change once early and then hold
        distinct = frames_differ(session, f"08_{name}", 20, 0.2, need=2, start='kag.process("emote_test.ks","*ev")')
        storage = EMOTE_EVS[name]
        session.steps.append({"name": f"08_{name}", "storage": storage, "distinct_frames": distinct})
        assert distinct >= 2, f"{name} ({storage}) looks static"
    text = (session.out / "game.log").read_text(errors="replace")
    # a requested name counts once the file it was placed at parsed; with the Chinese patch
    # title_bg*.mtn are placed at their xpck://./title_bg*_cn.mtn redirects
    parsed = set(re.findall(r"load parsed=1: (\S+\.mtn)", text))
    loaded = {name for name, placed in re.findall(r"load requested=(\S+\.mtn) trimmed=\S+ placed=(\S+)", text)
              if placed in parsed}
    missing = sorted({EMOTE_EVS[name] for name in names} - loaded)
    assert not missing, f"Motions never loaded: {missing}"


def emote_eyecatch(session):
    """E-mote presentation rate on an SD scene, and the eyecatch."""
    enter_game(session)
    session.ev(f"Storages.addAutoPath({json.dumps(str(TESTS) + '/')})")
    session.ev('tf.llljEv="sd001a01"')
    session.ev('kag.process("emote_test.ks","*ev")')
    session.wait("kag.inStable")
    session.step("08_sd_stats", 1)
    session.ev('kag.process("emote_test.ks","*eyecatch")')
    distinct = frames_differ(session, "08_eyecatch", 12, 0.2, need=4)
    session.steps.append({"name": "08_eyecatch", "distinct_frames": distinct,
                          "note": "Band characters must be on the stairs once state=1 (compare image/アイキャッチキャラ.png)."})
    assert distinct >= 4, "eyecatch looks static"


# Gallery movie entries (replay.ks *movie_<label>) and the file each must open.
MOVIES = {"op": "op.mp4", "ed_ena": "ed1_ena.wmv", "ed_anj": "ed2_anj.wmv", "ed_tuk": "ed3_tsu.wmv",
          "ed_rir": "ed4_rir.wmv", "ed_mik": "ed5_mik.wmv", "ed_nay": "ed6_nay.wmv", "ed_gra": "edg_gra.wmv"}
MOVIE_STATE = '(function(){var m=kag.movies[0];return %[status:m.lastStatus,position:m.position,total:m.totalTime,mode:SystemHook.currentState,storage:kag.conductor.curStorage];})()'


def start_movie(session, label, expect=None):
    opened = session.obj("CompatNative.movieStats()")["opened"]
    session.ev(f"tf.movie_play={json.dumps(label)}")
    session.ev('kag.process("custom.ks","*extra_movie_start")')
    session.wait(f'CompatNative.movieStats().opened > {opened} && kag.movies[0].lastStatus == "play" && kag.movies[0].position > 0', 30)
    stats = session.obj("CompatNative.movieStats()")
    expect = expect or MOVIES[label]
    assert stats["name"].lower().endswith("/" + expect), f"{label} opened {stats['name']}"


def back_in_gallery(session, timeout):
    session.wait('SystemHook.currentState == "cgmode" && kag.conductor.curStorage == "cgmode.ks" && kag.inStable', timeout)


def movies(session, labels):
    """Video: gallery movies play, advance, and end or are skipped back to the gallery."""
    session.jump("title.ks", "*cgmode", "cgmode")
    for label in labels:
        name = f"09_{label}"
        start_movie(session, label)
        # several EDs open on seconds of black; compare frames from mid-movie (also a seek test)
        session.ev("kag.movies[0].position = 30000")
        # Sample once playback has resumed after the seek (decoding up from the
        # previous keyframe takes a moment).
        session.wait("kag.movies[0].position > 30000 && kag.movies[0].position < 33000", 10)
        first = session.obj(MOVIE_STATE)
        distinct = frames_differ(session, name, 8, 0.2, need=2)
        session.wait(f"kag.movies[0].position >= {first['position'] + 500}", 5)  # advancing while playing
        second = session.obj(MOVIE_STATE)
        assert distinct >= 2, f"{label}: picture looks static"
        entry = {"name": name, "start": first, "later": second, "distinct_frames": distinct}
        if label == "op":
            session.step(f"{name}_playing", 1)  # presentation rate while the movie plays
            entry["stats"] = session.steps.pop()["stats"]
        how = "end"
        if label == "ed_anj":
            # right click / Esc are the skip inputs (rightClickHook / keyDownHook in
            # sysmovie); a left click only skips when tflags.sysmovie_skip allows it
            how = "right_click"
            session.click(960, 540, 3)
        elif label == "ed_tuk":
            how = "escape"
            session.key("Escape")
        else:
            # jump near the end and let it finish on its own
            session.ev("kag.movies[0].position = kag.movies[0].totalTime - 1500")
            session.wait("kag.movies[0].position >= kag.movies[0].totalTime - 1500", 10)
        started = time.monotonic()
        back_in_gallery(session, 30)
        entry.update(how=how, return_seconds=round(time.monotonic() - started, 2),
                     movie_stats=session.obj("CompatNative.movieStats()"))
        session.steps.append(entry)
        session.write_report()
        ms = entry["movie_stats"]
        print(f"{name}: {how} -> gallery in {entry['return_seconds']}s, frames={ms['frames']:.0f} "
              f"dropped={ms['dropped']:.0f} max_late={ms['max_late_ms']:.1f}ms underrun={ms['underrun_ms']:.1f}ms", flush=True)
        if how == "end":
            assert entry["return_seconds"] < 15, f"{label}: end of movie took too long to return"
        else:
            assert entry["return_seconds"] < 5, f"{label}: skip did not return promptly"
        assert ms["max_late_ms"] <= 60, f"{label}: video fell behind the audio clock ({ms['max_late_ms']}ms)"


# Every voice effect the scenarios reference (spelled as there: upper-case
# prefixes, lower-case files) plus the movie-sample reverb, with their tails (ms).
VOICE_EFFECTS = {"DL_メッセージ": 300, "RV_DSP_ライブマイク": 1000, "DL_回想": 600, "DSP_電話": 0,
                 "RV_ライブハウス": 1000, "DL_心の声": 300, "DSP_ドア越し": 0, "DSP_インターホン": 0,
                 "DSP_配信": 0, "RV_DSP_体育館マイク": 1000, "DSP_DL_校内放送": 400,
                 "DSP_DL_ライブマイク野外": 400, "DSP_ノートPC": 0, "RV_風呂": 1000, "DSP_放送": 0,
                 "DSP_テレビ": 0, "wf_reverb": 1000}
TEST_VOICE = "anj_000_0001.ogg"
# Levels are sampled inside the engine on every frame (a continuous handler), so a slow
# REPL round trip on a loaded machine cannot miss a buffer's peak or its end.
# One buffer per effect, all playing at once; each keeps its own filter chain and level.
PLAY_WITH_EFFECTS = """(function(names){
    global.llljFx = [];
    var info = [];
    for (var n = 0; n < names.count; n++) {
        var ex = kag.voiceEffectPlugin.loadFilter(names[n], false);
        var fs = [];
        for (var i = 0; i < ex.count; i++) fs.add(Scripts.eval(ex[i], void, void, VoiceEffectFactory));
        var b = new WaveSoundBuffer(kag);
        b.filters.assign(fs);
        b.open("TEST_VOICE");
        llljFx.add(%[buf: b, peak: 0]);
        info.add(%[exprs: ex, filters: fs.count]);
    }
    global.llljFxSample = function() {
        var now = System.getTickCount(), left = 0;
        for (var n = 0; n < llljFx.count; n++) {
            var f = llljFx[n];
            if (f.end !== void) continue;
            if (f.buf.status == "stop") { f.end = now; continue; }
            var v = +f.buf.sampleValue;
            if (v > f.peak) f.peak = v;
            left++;
        }
        if (!left) System.removeContinuousHandler(llljFxSample);
    };
    for (var n = 0; n < llljFx.count; n++) { llljFx[n].start = System.getTickCount(); llljFx[n].buf.play(); }
    System.addContinuousHandler(llljFxSample);
    return info;
})""".replace("TEST_VOICE", TEST_VOICE)
EFFECTS_RESULT = """(function(){
    var r = [];
    for (var n = 0; n < llljFx.count; n++) r.add(llljFx[n].end === void ? void : [llljFx[n].end - llljFx[n].start, llljFx[n].peak]);
    return r;
})()"""
# the target buffer receives mtrack_id from the open callback, as in MTrackHackedEnvMusicObject
OPEN_MULTITRACK = """(function(storages){
    global.llljMt = [];
    global.llljMtPeak = [];
    var r = [];
    for (var n = 0; n < storages.count; n++) {
        var b = new WaveSoundBuffer(kag);
        SystemConfig.MultiTrackTargetBuffer = b;
        b.open(storages[n]);
        SystemConfig.MultiTrackTargetBuffer = null;
        llljMt.add(b);
        llljMtPeak.add(0);
        r.add(%[id: b.mtrack_id, tracks: SystemConfig.MultiTrackSupport[b.mtrack_id]["*"]]);
    }
    global.llljMtSample = function() {
        for (var n = 0; n < llljMt.count; n++) { var v = +llljMt[n].sampleValue; if (v > llljMtPeak[n]) llljMtPeak[n] = v; }
    };
    for (var n = 0; n < llljMt.count; n++) llljMt[n].play();
    return r;
})"""
# Peak of every multi-track buffer since the previous call, then start a new window.
MULTITRACK_PEAKS = """(function(){
    var r = [];
    for (var n = 0; n < llljMtPeak.count; n++) { r.add(llljMtPeak[n]); llljMtPeak[n] = 0; }
    return r;
})()"""


def number(session, expression):
    return float(session.ev(f"string({expression})").strip('"'))


def peak_level(session, buf, seconds):
    peak, end = 0.0, time.monotonic() + seconds
    while time.monotonic() < end:
        peak = max(peak, number(session, f"{buf}.sampleValue"))
        time.sleep(0.03)
    return peak


def multitrack_window(session, seconds):
    """Peak level of every multi-track buffer over the next seconds (sampled in the engine)."""
    session.obj(MULTITRACK_PEAKS)
    session.ev("System.addContinuousHandler(llljMtSample)")
    time.sleep(seconds)
    session.ev("System.removeContinuousHandler(llljMtSample)")
    return session.obj(MULTITRACK_PEAKS)


def audio_link(session):
    session.ev('Plugins.link("getSample.dll")')  # sampleValue, as voice.tjs links it


def audio_voice(session):
    """Butterworth band pass from the game's own factory, and the game's voice path."""
    audio_link(session)
    # Butterworth band pass (dsp_電話 etc.); the DSPFilters design's amplitude response
    # is 0.155 / 0.990 / 0.112.
    session.ev('global.llljBp = Scripts.eval("BandPass_Butterworth(2,1900,2900)", void, void, VoiceEffectFactory)')
    gains = [number(session, f"CompatNative.waveFilterGain(llljBp, {hz})") for hz in (200, 1900, 8000)]
    session.steps.append({"name": "band_pass_response", "hz": [200, 1900, 8000], "gain": gains})
    for got, want in zip(gains, (0.155, 0.990, 0.112)):
        assert abs(got - want) < 0.05, f"band pass response {gains}"
    # CustomVoiceSoundBuffer splits "voice|effect" and inserts the chain; sampleValue
    # (getSample) drives the lip sync (LIPLEVEL1 0.01)
    session.ev(f'kag.voices[0]._start("{TEST_VOICE}|DSP_電話")')
    chain = number(session, "kag.voices[0].filters.count")
    lip = peak_level(session, "kag.voices[0]", 1.5)
    session.steps.append({"name": "voice_buffer_effect", "filters": chain, "peak_sample_value": lip})
    assert chain == 1 and lip > 0.01, f"voice buffer: filters={chain} sampleValue peak={lip}"


def audio_effects(session):
    """Every voice effect: audible, and the chain keeps ringing for its tail after the voice."""
    audio_link(session)
    total = number(session, f'(function(){{var b=new WaveSoundBuffer(kag);b.open("{TEST_VOICE}");var t=b.totalTime;invalidate b;return t;}})()')
    names = list(VOICE_EFFECTS)
    infos = session.obj(f"{PLAY_WITH_EFFECTS}({json.dumps(names, ensure_ascii=False)})")
    deadline = time.monotonic() + 15
    while None in (results := session.obj(EFFECTS_RESULT)):
        assert time.monotonic() < deadline, f"playback never ended: {[n for n, r in zip(names, results) if r is None]}"
        time.sleep(0.1)
    session.ev("(function(){ for (var n = 0; n < llljFx.count; n++) invalidate llljFx[n].buf; llljFx = void; })()")
    played_peaks = results
    failures = []
    for name, info, (played, level) in zip(names, infos, played_peaks):
        tail = VOICE_EFFECTS[name]
        session.steps.append({"name": f"voeff_{name}", "exprs": info["exprs"], "voice_ms": total,
                              "played_ms": played, "peak": level})
        print(f"{name}: {' + '.join(info['exprs'])} -> {played}ms (voice {total:.0f}ms, tail {tail}ms) peak {level:.4f}", flush=True)
        if not (info["exprs"] and info["filters"] == len(info["exprs"])):
            failures.append(f"{name}: {info}")
        if level <= 0.001:
            failures.append(f"{name}: silent output")
        if not total + tail * 0.8 - 300 <= played <= total + tail * 2 + 1200:
            failures.append(f"{name}: played {played}ms")
    assert not failures, failures


def audio_mtrack(session):
    """All 14 multi-track BGM files: they play, and the track mask mutes and restores them."""
    audio_link(session)
    tracks = sorted(p.name for p in (ROOT / "game/bgm").glob("*.mchx"))
    assert len(tracks) == 14, tracks
    mts = session.obj(f"{OPEN_MULTITRACK}({json.dumps(tracks)})")
    for storage, mt in zip(tracks, mts):
        assert mt["id"] and mt["tracks"], f"{storage}: {mt}"
    ids = json.dumps([mt["id"] for mt in mts])
    mask = "(function(ids, on, time){ for (var n = 0; n < ids.count; n++) WaveSoundBuffer.setMultiTrackMask(ids[n], void, on, time); })"
    full = multitrack_window(session, 1.5)
    session.ev(f"{mask}({ids}, 0, 0)")
    time.sleep(1.2)  # let already queued audio drain
    muted = multitrack_window(session, 0.5)
    session.ev(f"{mask}({ids}, 1, 300)")
    time.sleep(1.2)
    restored = multitrack_window(session, 1.0)
    session.ev("(function(){ for (var n = 0; n < llljMt.count; n++) invalidate llljMt[n]; llljMt = void; })()")
    failures = []
    for storage, mt, a, m, r in zip(tracks, mts, full, muted, restored):
        session.steps.append({"name": f"mtrack_{storage}", **mt, "peak_all": a, "peak_muted": m, "peak_restored": r})
        print(f"{storage}: {mt['tracks']} all={a:.4f} muted={m:.6f} restored={r:.4f}", flush=True)
        # some intros are silent, so either unmuted window may carry the music
        if not (max(a, r) > 0.0005 and m < 1e-6):
            failures.append(f"{storage}: mask had no effect")
    assert not failures, failures


# Screen points at 1920x1080. Flowchart: a connector between two chart nodes and the
# background beside it. Option Text page: four points on the colour wheel's hue ring.
FLOWCHART_CONNECTOR = [943, 580]
FLOWCHART_BACKGROUND = [1300, 600]
HSV_RING = [[1612, 232], [1737, 357], [1487, 357], [1612, 470]]
# transitions: (step name, label in games/lllj/tests/emote_test.ks, TJS run first or None)
TRANSITIONS = [
    ("mosaic_game", "trans_mosaic", None),  # the game's own table entries
    ("turn_game", "trans_turn", None),
    ("mosaic_extrans", "trans_method", 'tf.llljMethod="mosaic",tf.llljTime=750'),
    ("turn_extrans", "trans_method", 'tf.llljMethod="turn",tf.llljTime=1000'),
] + [(f"{method}_nagano", "trans_method", f'tf.llljMethod="{method}",tf.llljTime=700')
     for method in ("blurfade", "flutter", "multiripple", "scanline", "zoomfade", "spin")]


def luma(colour):
    return ((colour >> 16) & 255) * 0.3 + ((colour >> 8) & 255) * 0.59 + (colour & 255) * 0.11


def far_apart(colours, threshold=60):
    def distance(a, b):
        return max(abs(((a >> shift) & 255) - ((b >> shift) & 255)) for shift in (0, 8, 16))
    return all(distance(a, b) > threshold for i, a in enumerate(colours) for b in colours[i + 1:])


def png_pixels(session, path, points):
    """0xRRGGBB at each point of a captured PNG, read by the engine's own Bitmap loader."""
    return session.obj("(function(){ var b = new Bitmap(); b.load(%s); var p = %s, r = []; "
                       "for (var i = 0; i < p.count; i++) r.add(b.getPixel(p[i][0], p[i][1]) & 0xFFFFFF); "
                       "return r; })()" % (json.dumps(str(path), ensure_ascii=False), json.dumps(points)))


def exception_count(session):
    return (session.out / "game.log").read_text(errors="replace").count("exception occured")


def stripe_wipe(session):
    """Open the backlog from the game and time the system stripe wipe (layerStwCopy).
    cureff is true while kag.sysTransitionEffect is running and false once it has settled."""
    active = "(typeof kag.sysTransitionEffect == 'Object' && !!kag.sysTransitionEffect.cureff)"
    session.ev("SystemAction.backlog()")
    deadline = time.monotonic() + 10
    while session.ev(f"!!{active}") != "1":
        assert time.monotonic() < deadline, "backlog did not start the stripe wipe"
        time.sleep(0.01)
    started = time.monotonic()
    while session.ev(f"!!{active}") == "1":
        assert time.monotonic() < started + 10, "stripe wipe never settled"
        time.sleep(0.01)
    return round((time.monotonic() - started) * 1000)


# Chinese patch (batch 6). Click targets are the patch's own controls at 1920x1080: the title's
# language button and the rows of its language panel (langselect_right.pbd).
ZH_TITLE_LANG_BUTTON = (1836, 82)
ZH_PANEL_ROWS = {0: (1738, 165), 1: (1738, 237), 2: (1738, 310)}
ZH_LANG = "[kag.languageType, SystemConfig.uiLanguageType]"
ZH_WEBP = "街_店の前a_冬_cn.png"  # a WebP stored as .png in lllj_zh.pck


def zh_pick_language(session, index):
    session.key("alt+l")
    session.wait("typeof LanguageSelectPanel.Instance == 'Object' && isvalid LanguageSelectPanel.Instance", 10)
    time.sleep(1.2)  # the rows take clicks once the panel's open animation has finished
    session.click(*ZH_PANEL_ROWS[index])
    session.wait(f"kag.languageType == {index} && typeof LanguageSelectPanel.Instance != 'Object'", 10)


def zh_text(session):
    """Current line in every language, and the line the backlog recorded for it."""
    return session.obj("%[lang: world_object.player.curText.language, "
                       "shown: kag.historyLayer.data[kag.historyLayer.data.count-1].plaintext]")




def extras_static(session):
    """PackinOne / layerStwCopy exports that need no screen, and the msdfrender members."""
    size = session.obj("Layer.fetchImageSize('attention')")
    session.steps.append({"name": "fetchImageSize_attention", "size": size})
    assert size == [1920, 1080], f"Layer.fetchImageSize('attention') = {size}"
    octet = session.obj("(function(){ var dst = System.dataPath + 'extras_octet.png'; "
                        "Storages.saveOctet(dst, CompatNative.readOctet('attention.png')); "
                        "var back = CompatNative.readOctet(dst), src = CompatNative.readOctet('attention.png'); "
                        "return [Storages.isExistentStorage(dst), back.length, back === src, "
                        "back === CompatNative.readOctet('aboutdialog.png')]; })()")
    session.steps.append({"name": "saveOctet_roundtrip", "result": {"exists": octet[0], "length": octet[1],
                          "equal_to_source": octet[2], "equal_to_other_image": octet[3]}})
    # TJS booleans arrive as 1 / 0 through Scripts.toJSONString
    assert octet[0] and octet[1] > 0 and octet[2] and not octet[3], f"saveOctet round trip: {octet}"
    folder = session.obj("System.getKnownFolderPath('Captures', 34816)")
    session.steps.append({"name": "getKnownFolderPath_captures", "path": folder, "is_dir": Path(folder).is_dir()})
    assert folder, "System.getKnownFolderPath('Captures') returned nothing"
    assert session.ev("typeof Layer.makeGlyphMSDF") == '"Object"', "msdfrender members are not attached"


def extras_ui(session):
    """layerExDraw (flowchart lines, HSV colour wheel), msdfrender atlas text (Text page
    sliders, flowchart headers) and the gamepad page."""
    session.jump("custom.ks", "*flowchart", "scnchart")
    assert session.ev("typeof GdiPlus") == '"Object"', "GdiPlus (layerExDraw) is not registered"
    # the chart draws its connectors after the screen settles
    deadline = time.monotonic() + 10
    while True:
        session.step("13_flowchart", 0.1)
        line, background = png_pixels(session, session.out / "13_flowchart.png", [FLOWCHART_CONNECTOR, FLOWCHART_BACKGROUND])
        if luma(line) > luma(background) + 80 or time.monotonic() > deadline:
            break
    session.steps[-1]["connector_colour"] = f"{line:06X}"
    session.steps[-1]["background_colour"] = f"{background:06X}"
    assert luma(line) > luma(background) + 80, f"flowchart connector is not drawn: {line:06X} over {background:06X}"
    session.jump("scnchart.ks", "*back", "title")
    session.step("13_flowchart_back")

    # option Text page: HSV colour wheel (layerExDraw) and atlas slider digits (msdfrender)
    session.jump("title.ks", "*option", "option")
    session.ev('Current.page=4,kag.process("option.ks","*page")')
    session.wait('Current.propget("page") == 4 && kag.inStable')
    deadline = time.monotonic() + 10
    while True:
        session.step("14_option_text", 0.1)
        ring = png_pixels(session, session.out / "14_option_text.png", HSV_RING)
        if far_apart(ring) or time.monotonic() > deadline:
            break
    session.steps[-1]["hsv_ring"] = [f"{c:06X}" for c in ring]
    assert far_apart(ring), f"colour wheel has no hue variation: {[f'{c:06X}' for c in ring]}"

    # option gamepad page (index 9): the game's pad input through -joypad=dinput
    session.ev('Current.page=9,kag.process("option.ks","*page")')
    session.wait('Current.propget("page") == 9 && kag.inStable')
    session.step("15_option_gamepad")
    session.jump("option.ks", "*back", "title")


def extras_wipe(session):
    """The backlog opens from the game through the system stripe wipe (layerStwCopy)."""
    enter_game(session, 2)
    wipe_ms = stripe_wipe(session)
    session.steps.append({"name": "stripe_wipe_backlog", "active_ms": wipe_ms})
    assert 50 <= wipe_ms <= 5000, f"stripe wipe lasted {wipe_ms}ms"
    session.wait('SystemHook.currentState == "backlog"')
    session.step("16_backlog")
    back_to_game(session, "backlog.ks")


def extras_transitions(session, names):
    """Transitions on the striped overlay (emote_test.ks). Each must run to completion without
    a game exception and return to a stable state. The distinct mid-transition frame count
    is recorded, not asserted: whether the effect is drawn is checked against Wine by
    games/lllj/tests/emote_compare.py."""
    session.ev(f"Storages.addAutoPath({json.dumps(str(TESTS) + '/')})")
    enter_game(session, 2)
    for name, label, settings in TRANSITIONS:
        if name not in names:
            continue
        before = exception_count(session)
        if settings:
            session.ev(settings)
        session.ev(f'kag.process("emote_test.ks","*{label}")')
        distinct = frames_differ(session, f"18_{name}", 3, 0.15)
        session.wait("kag.inStable", 30)
        session.capture(f"18_{name}_done")
        new = exception_count(session) - before
        session.steps.append({"name": f"18_{name}", "label": label, "settings": settings,
                              "distinct_mid_frames": distinct, "new_exceptions": new})
        print(f"{name}: {distinct} distinct frames, {new} new exceptions", flush=True)
        assert new == 0, f"{name} raised {new} game exception(s); see game.log"


def zh_lang_ui(session):
    """Chinese patch: pack mount, redirects, the title's language toggle, Alt+L, WebP."""
    assert session.ev("CompatNative.xpckEnabled()") == "1", "lllj_zh.pck is not mounted"
    assert session.obj(ZH_LANG) == [1, 1], "the patch starts in Simplified Chinese"
    placed = lambda name: json.loads(session.ev(f"Storages.getPlacedPath({json.dumps(name)})"))
    assert placed("title_bg.mtn") == "xpck://./title_bg_cn.mtn"
    session.step("zh_01_title_cn")

    # the title's own toggle (jp <-> cn, text and UI); the redirect follows on the next open
    session.click(*ZH_TITLE_LANG_BUTTON)
    session.wait(f"{ZH_LANG}[0] == 0 && {ZH_LANG}[1] == 0 && SystemHook.currentState == 'title' && kag.inStable", 20)
    assert placed("title_bg.mtn").endswith("/motion/title_bg.mtn")
    assert placed("op.ass") == "", "Japanese has no OP subtitles"
    session.step("zh_02_title_jp")
    session.click(*ZH_TITLE_LANG_BUTTON)
    session.wait(f"{ZH_LANG}[0] == 1 && {ZH_LANG}[1] == 1 && SystemHook.currentState == 'title' && kag.inStable", 20)

    # Alt+L panel on the title: Traditional text keeps the cn UI (the patch has no tw UI images)
    zh_pick_language(session, 2)
    session.wait("SystemHook.currentState == 'title' && kag.inStable", 20)
    assert session.obj(ZH_LANG) == [2, 1]
    assert placed("op.ass") == "xpck://./op_cht.ass"
    session.step("zh_03_title_tw")

    # WebP data under a .png name: header and pixels (reference values from dwebp)
    webp = json.dumps(ZH_WEBP, ensure_ascii=False)  # TJS strings have no \u escapes
    size = session.obj(f"Layer.fetchImageSize({webp})")
    pixel = int(session.ev(f"(function(){{var l=new Layer(kag,kag.fore.base);l.loadImages({webp});"
                           f"var p=l.getMainPixel(960,540);invalidate l;return p;}})()"))
    assert size == [2120, 1280] and pixel & 0xFFFFFF == 0x4C4E51, (size, hex(pixel))


def zh_dialogue(session):
    """Chinese patch: dialogue and backlog text in Traditional, then Simplified."""
    zh_pick_language(session, 2)
    session.wait("SystemHook.currentState == 'title' && kag.inStable", 20)
    enter_game(session, 3)
    text = zh_text(session)
    assert len(text["lang"]) == 3 and text["shown"] == text["lang"][2]["searchtext"], text
    assert text["lang"][1]["searchtext"] != text["lang"][2]["searchtext"] != text["lang"][0]["searchtext"], text
    assert any("一" <= c <= "鿿" for c in text["shown"]), text
    session.step("zh_04_dialogue_tw")
    zh_pick_language(session, 1)  # in game: text language only, like the Windows menu
    assert session.obj(ZH_LANG) == [1, 1]
    advance(session, 1)
    text = zh_text(session)
    assert text["shown"] == text["lang"][1]["searchtext"], text
    session.step("zh_05_dialogue_cn")
    session.ev("SystemAction.backlog()")
    session.wait('SystemHook.currentState == "backlog"')
    session.step("zh_06_backlog")
    back_to_game(session, "backlog.ks")


def zh_subtitles(session):
    """Chinese patch: OP subtitles in the current language; Japanese plays without them."""
    session.jump("title.ks", "*cgmode", "cgmode")
    for index, expect in ((1, "op_chs.ass"), (2, "op_cht.ass"), (0, None)):
        session.ev(f"SetLanguageType({index})")
        start_movie(session, "op")
        session.ev("kag.movies[0].position = 21000")  # first lyric: 19.71-24.54s
        session.wait("kag.movies[0].position >= 21500", 10)
        stats = session.obj("CompatNative.movieStats()")
        if expect:
            sub = stats.get("subtitle")
            assert sub and sub["file"] == expect and sub["max_images"] > 0, stats
            session.step(f"zh_07_{expect[:-4]}")
        else:
            assert "subtitle" not in stats, stats
        session.click(960, 540, 3)  # right click skips
        back_in_gallery(session, 15)


def zh_mv1(session):
    """Chinese patch: its music videos exist only in the pack, with their own subtitles."""
    session.jump("title.ks", "*cgmode", "cgmode")
    session.ev("SetLanguageType(1)")
    start_movie(session, "mv1", "mv1.mp4")
    session.ev("kag.movies[0].position = 13000")  # 12.34-16.53s
    session.wait("kag.movies[0].position >= 13500", 10)
    stats = session.obj("CompatNative.movieStats()")
    assert stats["name"].startswith("xpck://") and stats.get("subtitle", {}).get("file") == "mv1_chs.ass", stats
    assert stats["subtitle"]["max_images"] > 0, stats
    session.step("zh_08_mv1_chs")
    session.click(960, 540, 3)
    back_in_gallery(session, 15)


# The supplied Windows continue save of the Chinese build (save_seed of the winsave case).
WINSAVE_SOURCE = ROOT.parent / "limelight/savedata_zh"


def winsave(session):
    """Read the supplied Windows continue save through the game's real load path."""
    import hashlib
    import shutil
    from pbd_decode import decode
    source = WINSAVE_SOURCE
    original = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}
    expected = decode((source / "data_continue.jpg").read_bytes())["core"]["scenePlayer"]
    for name in ("datasc.ksd", "datasu.ksd", "datavc.ksd", "data_continue.jpg"):
        loader = "evalStorage" if name in ("datasc.ksd", "datasu.ksd") else "loadDataPack"
        result = session.ev(f'typeof Scripts.{loader}(System.dataPath+{json.dumps(name)})')
        assert result == '"Object"', (name, result)
    summary = session.obj('(function(){var d=Scripts.loadDataPack(System.dataPath+"data_continue.jpg");return %[id:d.id,coreType:typeof d.core,userType:typeof d.user];})()')
    assert session.ev('kag.loadOtherDataID(%[id:"unrelated-game-id"])') == "0"
    # The ordinary UI loader also resets SystemHook and runs loadinit.ks;
    # loading only the low-level bookmark leaves the title's UI state set.
    index = int(session.ev("SystemConfig.savedataTopIndex"))
    target = json.loads(session.ev(f"kag.getBookMarkFileNameAtNum({index})"))
    target = Path(target.removeprefix("file://."))
    assert target.resolve().is_relative_to(session.out / "saves"), target
    shutil.copyfile(session.out / "saves/data_continue.jpg", target)
    session.ev(f"loadFunction({index})")
    session.wait('SystemHook.currentState == "game" && typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    session.wait("kag.inStable")
    state = session.step("windows_continue_loaded")
    assert state["scene"] + state["label"] == expected["scene"], (expected, state)
    assert state["point"] == expected["point"], (expected, state)
    dimensions = session.obj('(function(){var l=new Layer(kag,kag.primaryLayer);l.loadImages(System.dataPath+"data_continue.jpg");var d=%[width:l.imageWidth,height:l.imageHeight];invalidate l;return d;})()')
    assert dimensions["width"] > 0 and dimensions["height"] > 0, dimensions
    advance(session, 1)
    later = session.step("windows_continue_advanced")
    assert (later["scene"], later["cursor"]) != (state["scene"], state["cursor"])
    # With lllj_zh.pck mounted the save is the game's own (localized) ID and the
    # restored system settings keep its Chinese text language.
    assert json.loads(session.ev("kag.saveDataID")) == summary["id"], summary
    text = session.obj("%[lang: kag.languageType, shown: kag.historyLayer.data[kag.historyLayer.data.count-1].plaintext,"
                       " line: world_object.player.curText.language[kag.languageType].searchtext]")
    assert text["lang"] in (1, 2) and text["shown"] == text["line"], text
    assert original == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}
    session.steps.append({"name": "save_summary", "save": summary, "text": text, "thumbnail": dimensions,
                          "expected_position": {"scene": expected["scene"], "point": expected["point"]}})
