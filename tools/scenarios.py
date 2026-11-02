#!/usr/bin/env python3
"""Isolated game scenarios with assertions, screenshots and presentation stats."""
import argparse
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

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from repl import evaluate
from import_saves import import_saves


class Session:
    def __init__(self, out, extra=(), gallery_seed=False, save_seed=None, headless=True):
        self.out = Path(out).resolve()
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
            [str(ROOT / "run.sh"), f"-datapath={self.out}/saves/", "-lllj_fps=yes", "-language=en", *extra],
            cwd=ROOT, env=env, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        self.steps = []

    def click(self, x, y, button=1):
        env = os.environ.copy()
        env["REPLCH"] = str(self.channel)
        subprocess.run([str(ROOT / "tools/click.sh"), str(x), str(y), str(button)], env=env, check=True, stdout=subprocess.DEVNULL)

    def key(self, key):
        env = os.environ.copy()
        env["REPLCH"] = str(self.channel)
        subprocess.run([str(ROOT / "tools/click.sh"), "key", key], env=env, check=True, stdout=subprocess.DEVNULL)

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate() # launcher stops game before its X server
            try:
                self.proc.wait(timeout=5)
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
            if self.channel.is_dir():
                last = self.ev(f"!!({expression})")
                if last == "1":
                    return
            time.sleep(0.1)
        raise TimeoutError(f"Condition timed out: {expression} (last: {last})")

    def boot(self):
        self.wait('typeof global.kag == "Object" && typeof global.SystemHook == "Object" && kag.conductor.curStorage == "custom.ks"', 60)
        self.ev(f"Scripts.execStorage({json.dumps(str(ROOT / 'tools/scenarios.tjs'))})")

    def finish_startup(self):
        # Let caution/Logo cleanup run: jumping out early leaves its transparent
        # SysCoverLayer above the title buttons and creates a false input bug.
        self.ev('LLLJNative.resetFrameStats("startup_logos")')
        if self.ev('SystemHook.currentState != "title"') == "1":
            if self.headless:
                self.click(720, 400) # normal click-to-skip caution path
            else:
                self.ev("kag.onPrimaryClick()")
        self.wait('SystemHook.currentState == "title" && kag.inStable', 90)
        stats = self.obj("LLLJNative.frameStats()")
        self.steps.append({"name": "startup_logos", "state": self.obj("llljTestState()"),
                           "stats": stats, "screenshot_size": self.capture("00_startup_finished"),
                           "note": "Normal caution/Logo traversal."})
        self.write_report()

    def jump(self, storage, label, mode=None):
        self.ev(f"llljTestJump({json.dumps(storage)},{json.dumps(label)})")
        if mode:
            self.wait(f"SystemHook.currentState == {json.dumps(mode)}")
        time.sleep(0.45)

    def capture(self, name):
        path = self.out / f"{name}.png"
        self.ev(f"System.captureScreen({json.dumps(str(path))})")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size > 24:
                head = path.read_bytes()[:24]
                if head[:8] != b"\x89PNG\r\n\x1a\n":
                    raise AssertionError(f"Invalid screenshot: {path}")
                return list(struct.unpack(">II", head[16:24]))
            time.sleep(0.05)
        raise TimeoutError(f"Screenshot missing: {path}")

    def step(self, name, seconds=1):
        self.ev(f"LLLJNative.resetFrameStats({json.dumps(name)})")
        time.sleep(seconds)
        stats = self.obj("LLLJNative.frameStats()")
        assert stats["detailed"] == 1, "Build with KRKRZ_DRAW_STATS=ON"
        assert stats["frames"] > 0, f"No presentations in {name}"
        state = self.obj("llljTestState()")
        dimensions = self.capture(name)
        self.steps.append({"name": name, "state": state, "stats": stats, "screenshot_size": dimensions})
        self.steps[-1]["storage_stats"] = self.obj("LLLJNative.storageStats()")
        self.write_report()
        print(f"{name}: {state['mode']} {state['storage']} ({stats['fps']:.1f} present/s)", flush=True)
        return state

    def write_report(self, **extra):
        import re
        text = (self.out / "game.log").read_text(errors="replace")
        renderers = re.findall(r"\(lllj\) renderer: (.*)", text)
        report = {"steps": self.steps, "environment": ("Xvfb" if self.headless else "desktop") + "; completed swaps, not scanout refresh", "renderers": renderers, **extra}
        (self.out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))


def check_log(path):
    # Exact call sites only. All unmatched exceptions remain failures.
    import re
    text = Path(path).read_text(errors="replace")
    matches = list(re.finditer(r"exception occured at ([^(]+)\(\d+\)\[\(function\) ([^\]]+)\], VM ip = (\d+)", text))
    known, unknown = [], []
    for i, match in enumerate(matches):
        site = match.groups()
        block = text[match.end():matches[i + 1].start() if i + 1 < len(matches) else len(text)]
        if site == ("voiceeffect.tjs", "Verb", "6") and '(string)"WaveSoundBuffer"' in block and '(string)"StkFreeVerb"' in block:
            known.append({"site": list(site), "reason": "batch 4: missing DSP"})
        else:
            unknown.append(site)
    # Also reject fatal errors and exception headers the parser did not recognize.
    headers = text.count("exception occured")
    if "source unsupported" in text:  # E-mote content the runtime cannot draw
        raise AssertionError("E-mote reported an unsupported source; see game.log")
    if headers != len(matches) or unknown or "[dialog] Information:" in text or "prelude error:" in text:
        raise AssertionError(f"Unhandled/unexpected game errors: {unknown}; headers={headers}, parsed={len(matches)}")
    return known


def advance(session, lines):
    for _ in range(lines):
        before = session.obj("llljTestState()")
        for attempt in range(12):
            session.ev("kag.onPrimaryClick()")
            time.sleep(0.3)
            after = session.obj("llljTestState()")
            if (after.get("scene"), after.get("cursor")) != (before.get("scene"), before.get("cursor")):
                break
        else:
            raise AssertionError(f"Dialogue failed to advance: {before}")
    session.wait("kag.inStable")


def smoke(session, lines, sample_seconds):
    session.boot()
    session.step("01_attention")
    session.finish_startup()
    session.step("02_title", sample_seconds)
    session.jump("title.ks", "*game", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    advance(session, lines)
    session.step("03_dialogue", sample_seconds)
    session.ev("SystemAction.backlog()")
    session.wait('SystemHook.currentState == "backlog"')
    session.step("04_backlog")
    session.jump("backlog.ks", "*back", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0')

    # Normal save page plus real serialized game round trips (including a
    # persistent game flag), rather than merely checking that a file exists.
    session.ev("SystemAction.save()")
    session.wait('SystemHook.currentState == "save"')
    session.step("05_save_page")
    session.jump("save.ks", "*back", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0')
    session.wait("kag.inStable")
    session.ev("SystemAction.load()")
    session.wait('SystemHook.currentState == "load"')
    session.step("05_load_page")
    session.jump("load.ks", "*back", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0')
    for name, index in (("normal", "SystemConfig.savedataTopIndex"), ("quick", "SystemAction.qsaveBookmarkNumber")):
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

    session.jump("start.ks", "*gameend_title", "title")
    gallery(session)
    settings(session)
    controls(session)
    return check_log(session.out / "game.log")


def gallery(session):
    for mode in ("cgmode", "scenemode", "exchview"):
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


def settings(session):
    session.jump("title.ks", "*option", "option")
    pages = session.obj('Current.propget("pagetabs")')
    assert len(pages) == 10, pages
    for i, page in enumerate(pages):
        session.ev(f'Current.page={i},kag.process("option.ks","*page")')
        session.wait(f'Current.propget("page") == {i}')
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
    time.sleep(1)
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


def frames_differ(session, prefix, count, interval):
    """Capture count frames; return how many distinct images were seen."""
    import hashlib
    digests = set()
    for k in range(count):
        time.sleep(interval)
        session.capture(f"{prefix}_{k:02d}")
        digests.add(hashlib.sha256((session.out / f"{prefix}_{k:02d}.png").read_bytes()).hexdigest())
    return len(digests)


def emote(session):
    """E-mote (batch 2): logos, title background, SD scenes and the eyecatch."""
    session.boot()
    session.click(720, 400)  # leave caution; logos are E-mote motions
    logos = frames_differ(session, "08_logo", 8, 0.5)
    session.wait('SystemHook.currentState == "title" && kag.inStable', 90)
    session.step("08_title")
    session.steps[-1]["distinct_logo_frames"] = logos
    assert logos >= 4, f"Logo motions look static ({logos} distinct frames)"
    assert session.ev("typeof global.Motion") == '"Object"'
    session.jump("title.ks", "*game", "game")
    session.wait('typeof world_object.player == "Object" && world_object.player.curPoint > 0', 60)
    session.ev(f"Storages.addAutoPath({json.dumps(str(ROOT / 'tools') + '/')})")
    # Every .mtn the title ships: the first [ev] of each SD file (31), the
    # nine title backgrounds (title_bg7 is already shown at startup with the
    # all-CG save), and the eyecatch below. Each must load, draw and animate.
    for name in EMOTE_EVS:
        session.ev(f"tf.llljEv={json.dumps(name)}")
        session.ev('kag.process("emote_test.ks","*ev")')
        distinct = frames_differ(session, f"08_{name}", 5, 0.35)
        storage = EMOTE_EVS[name]
        session.steps.append({"name": f"08_{name}", "storage": storage, "distinct_frames": distinct})
        assert distinct >= 2, f"{name} ({storage}) looks static"
    session.ev('LLLJNative.resetFrameStats("08_sd_stats")')
    session.ev('tf.llljEv="sd001a01"')
    session.ev('kag.process("emote_test.ks","*ev")')
    session.step("08_sd_stats", 3)
    session.ev('kag.process("emote_test.ks","*eyecatch")')
    distinct = frames_differ(session, "08_eyecatch", 10, 0.25)
    session.steps.append({"name": "08_eyecatch", "distinct_frames": distinct,
                          "note": "Band characters must be on the stairs once state=1 (compare image/アイキャッチキャラ.png)."})
    assert distinct >= 4, "eyecatch looks static"
    text = (session.out / "game.log").read_text(errors="replace")
    missing = sorted({v for v in EMOTE_EVS.values()} - set(re.findall(r"load parsed=1: \S*[/>]([^/>\s]+\.mtn)", text)))
    assert not missing, f"Motions never loaded: {missing}"
    session.write_report()
    return check_log(session.out / "game.log")


# Gallery movie entries (replay.ks *movie_<label>) and the file each must open.
MOVIES = {"op": "op.mp4", "ed_ena": "ed1_ena.wmv", "ed_anj": "ed2_anj.wmv", "ed_tuk": "ed3_tsu.wmv",
          "ed_rir": "ed4_rir.wmv", "ed_mik": "ed5_mik.wmv", "ed_nay": "ed6_nay.wmv", "ed_gra": "edg_gra.wmv"}
MOVIE_STATE = '(function(){var m=kag.movies[0];return %[status:m.lastStatus,position:m.position,total:m.totalTime,mode:SystemHook.currentState,storage:kag.conductor.curStorage];})()'


def start_movie(session, label, expect=None):
    opened = session.obj("LLLJNative.movieStats()")["opened"]
    session.ev(f"tf.movie_play={json.dumps(label)}")
    session.ev('kag.process("custom.ks","*extra_movie_start")')
    session.wait(f'LLLJNative.movieStats().opened > {opened} && kag.movies[0].lastStatus == "play" && kag.movies[0].position > 0', 30)
    stats = session.obj("LLLJNative.movieStats()")
    expect = expect or MOVIES[label]
    assert stats["name"].lower().endswith("/" + expect), f"{label} opened {stats['name']}"


def back_in_gallery(session, timeout):
    session.wait('SystemHook.currentState == "cgmode" && kag.conductor.curStorage == "cgmode.ks" && kag.inStable', timeout)


def movie(session, vomstyle=None):
    """Video (batch 3): every gallery movie plays, advances, ends or is skipped back to the gallery."""
    session.boot()
    session.finish_startup()
    session.jump("title.ks", "*cgmode", "cgmode")
    labels = ["op"] if vomstyle else list(MOVIES)
    for i, label in enumerate(labels):
        name = f"09_{label}" + (f"_{vomstyle}" if vomstyle else "")
        # SelectMovieFile only prefers .mp4 for the Media Foundation style
        expect = "op.wmv" if vomstyle and vomstyle != "mfevr" else None
        start_movie(session, label, expect)
        # several EDs open on seconds of black; compare frames from mid-movie (also a seek test)
        session.ev("kag.movies[0].position = 30000")
        session.wait("kag.movies[0].position >= 30000 && kag.movies[0].position < 33000", 10)
        first = session.obj(MOVIE_STATE)
        distinct = frames_differ(session, name, 3, 0.5)
        second = session.obj(MOVIE_STATE)
        assert distinct >= 2, f"{label}: picture looks static"
        assert second["position"] - first["position"] >= 1000, f"{label}: position did not advance {first} -> {second}"
        entry = {"name": name, "start": first, "later": second, "distinct_frames": distinct}
        if label == "op":
            session.step(f"{name}_playing", 3)  # presentation rate while the movie plays
            entry["stats"] = session.steps.pop()["stats"]
        how = "end"
        if not vomstyle and label == "ed_anj":
            # right click / Esc are the skip inputs (rightClickHook / keyDownHook in
            # sysmovie); a left click only skips when tflags.sysmovie_skip allows it
            how = "right_click"
            session.click(960, 540, 3)
        elif not vomstyle and label == "ed_tuk":
            how = "escape"
            session.key("Escape")
        else:
            # jump near the end and let it finish on its own
            session.ev("kag.movies[0].position = kag.movies[0].totalTime - 3000")
            session.wait("kag.movies[0].position >= kag.movies[0].totalTime - 3000", 10)
        started = time.monotonic()
        back_in_gallery(session, 30)
        entry.update(how=how, return_seconds=round(time.monotonic() - started, 2),
                     movie_stats=session.obj("LLLJNative.movieStats()"))
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
    session.capture("09_back_in_gallery")
    return check_log(session.out / "game.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("lines", nargs="?", type=int, default=6)
    parser.add_argument("--sample-seconds", type=float, default=1)
    parser.add_argument("--only", choices=("all", "gallery", "settings", "controls", "emote", "movie"), default="all")
    parser.add_argument("--vomstyle", choices=("overlay", "mixer", "layer", "mfevr"),
                        help="movie: force a VideoOverlay mode and only play the OP")
    args = parser.parse_args()
    extra = [f"-vomstyle={args.vomstyle}"] if args.vomstyle else []
    session = Session(args.out, extra, gallery_seed=True)
    try:
        if args.only == "all":
            known = smoke(session, args.lines, args.sample_seconds)
        elif args.only == "emote":
            known = emote(session)
        elif args.only == "movie":
            known = movie(session, args.vomstyle)
        else:
            session.boot()
            session.finish_startup()
            globals()[args.only](session)
            known = check_log(session.out / "game.log")
        session.write_report(passed=True, known_exceptions=known)
        print(f"PASS: {session.out}/report.json")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    main()
