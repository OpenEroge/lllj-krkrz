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
    known, unknown = [], [match.groups() for match in matches]
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


# Every voice effect the scenarios reference (spelled as there: upper-case
# prefixes, lower-case files) plus the movie-sample reverb, with their tails (ms).
VOICE_EFFECTS = {"DL_メッセージ": 300, "RV_DSP_ライブマイク": 1000, "DL_回想": 600, "DSP_電話": 0,
                 "RV_ライブハウス": 1000, "DL_心の声": 300, "DSP_ドア越し": 0, "DSP_インターホン": 0,
                 "DSP_配信": 0, "RV_DSP_体育館マイク": 1000, "DSP_DL_校内放送": 400,
                 "DSP_DL_ライブマイク野外": 400, "DSP_ノートPC": 0, "RV_風呂": 1000, "DSP_放送": 0,
                 "DSP_テレビ": 0, "wf_reverb": 1000}
TEST_VOICE = "anj_000_0001.ogg"
PLAY_WITH_EFFECT = """(function(name){
    var ex = kag.voiceEffectPlugin.loadFilter(name, false);
    var fs = [];
    for (var i = 0; i < ex.count; i++) fs.add(Scripts.eval(ex[i], void, void, VoiceEffectFactory));
    var b = new WaveSoundBuffer(kag);
    b.filters.assign(fs);
    b.open("%s");
    b.play();
    global.llljFx = %%[buf: b, start: System.getTickCount()];
    return %%[exprs: ex, filters: fs.count];
})""" % TEST_VOICE
# the target buffer receives mtrack_id from the open callback, as in MTrackHackedEnvMusicObject
OPEN_MULTITRACK = """(function(storage){
    var b = new WaveSoundBuffer(kag);
    SystemConfig.MultiTrackTargetBuffer = b;
    b.open(storage);
    SystemConfig.MultiTrackTargetBuffer = null;
    b.play();
    global.llljMt = b;
    return %[id: b.mtrack_id, tracks: SystemConfig.MultiTrackSupport[b.mtrack_id]["*"]];
})"""


def number(session, expression):
    return float(session.ev(f"string({expression})").strip('"'))


def peak_level(session, buf, seconds):
    peak, end = 0.0, time.monotonic() + seconds
    while time.monotonic() < end:
        peak = max(peak, number(session, f"{buf}.sampleValue"))
        time.sleep(0.03)
    return peak


def audio(session):
    """Voice effects and multi-track BGM (batch 4)."""
    session.boot()
    session.finish_startup()  # settle at the title like the other scenarios
    session.ev('Plugins.link("getSample.dll")')  # sampleValue, as voice.tjs links it
    # Butterworth band pass (dsp_電話 etc.) from the game's own factory; the
    # DSPFilters design's amplitude response is 0.155 / 0.990 / 0.112.
    session.ev('global.llljBp = Scripts.eval("BandPass_Butterworth(2,1900,2900)", void, void, VoiceEffectFactory)')
    gains = [number(session, f"LLLJNative.waveFilterGain(llljBp, {hz})") for hz in (200, 1900, 8000)]
    session.steps.append({"name": "band_pass_response", "hz": [200, 1900, 8000], "gain": gains})
    for got, want in zip(gains, (0.155, 0.990, 0.112)):
        assert abs(got - want) < 0.05, f"band pass response {gains}"

    total = number(session, f'(function(){{var b=new WaveSoundBuffer(kag);b.open("{TEST_VOICE}");var t=b.totalTime;invalidate b;return t;}})()')
    for name, tail in VOICE_EFFECTS.items():
        info = session.obj(f"{PLAY_WITH_EFFECT}({json.dumps(name, ensure_ascii=False)})")
        assert info["exprs"] and info["filters"] == len(info["exprs"]), f"{name}: {info}"
        level, deadline = 0.0, time.monotonic() + 10
        while (value := number(session, 'llljFx.buf.status == "stop" ? -1 : llljFx.buf.sampleValue')) >= 0:
            assert time.monotonic() < deadline, f"{name}: playback never ended"
            level = max(level, value)
            time.sleep(0.03)
        elapsed = int(session.ev("System.getTickCount() - llljFx.start"))
        session.ev("invalidate llljFx.buf")
        session.steps.append({"name": f"voeff_{name}", "exprs": info["exprs"], "voice_ms": total,
                              "played_ms": elapsed, "peak": level})
        print(f"{name}: {' + '.join(info['exprs'])} -> {elapsed}ms (voice {total:.0f}ms, tail {tail}ms)", flush=True)
        assert level > 0.001, f"{name}: silent output"
        # the chain keeps ringing for the effects' extend time after the voice ends
        assert total + tail * 0.8 - 300 <= elapsed <= total + tail * 2 + 1200, f"{name}: played {elapsed}ms"
    # the game's own voice path: CustomVoiceSoundBuffer splits "voice|effect" and
    # inserts the chain; sampleValue (getSample) drives the lip sync (LIPLEVEL1 0.01)
    session.ev(f'kag.voices[0]._start("{TEST_VOICE}|DSP_電話")')
    chain = number(session, "kag.voices[0].filters.count")
    lip = peak_level(session, "kag.voices[0]", 1.5)
    session.steps.append({"name": "voice_buffer_effect", "filters": chain, "peak_sample_value": lip})
    assert chain == 1 and lip > 0.01, f"voice buffer: filters={chain} sampleValue peak={lip}"
    session.write_report()

    tracks = sorted(p.name for p in (ROOT / "game/bgm").glob("*.mchx"))
    assert len(tracks) == 14, tracks
    for storage in tracks:
        mt = session.obj(f"{OPEN_MULTITRACK}({json.dumps(storage)})")
        assert mt["id"] and mt["tracks"], f"{storage}: {mt}"
        mid = json.dumps(mt["id"])
        full = peak_level(session, "llljMt", 1.5)
        session.ev(f"WaveSoundBuffer.setMultiTrackMask({mid}, void, 0, 0)")
        time.sleep(1.2)  # let already queued audio drain
        muted = peak_level(session, "llljMt", 0.5)
        session.ev(f"WaveSoundBuffer.setMultiTrackMask({mid}, void, 1, 300)")
        time.sleep(1.2)
        restored = peak_level(session, "llljMt", 1.0)
        session.ev("invalidate llljMt")
        session.steps.append({"name": f"mtrack_{storage}", **mt, "peak_all": full, "peak_muted": muted, "peak_restored": restored})
        print(f"{storage}: {mt['tracks']} all={full:.4f} muted={muted:.6f} restored={restored:.4f}", flush=True)
        # some intros are silent, so either unmuted window may carry the music
        assert max(full, restored) > 0.0005 and muted < 1e-6, f"{storage}: mask had no effect"
    session.write_report()
    return check_log(session.out / "game.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("lines", nargs="?", type=int, default=6)
    parser.add_argument("--sample-seconds", type=float, default=1)
    parser.add_argument("--only", choices=("all", "gallery", "settings", "controls", "emote", "movie", "audio"), default="all")
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
        elif args.only == "audio":
            known = audio(session)
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
