#!/usr/bin/env python3
"""The scenario cases: what each runs, roughly how long it takes, and which sources it covers.
Run them through games/lllj/tests/run.py; `cases.py OUT NAME [engine args...]` runs one
case in the foreground (that is what run.py starts for each case)."""
from dataclasses import dataclass, field
from pathlib import Path
import sys
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scenarios as sc  # noqa: E402


@dataclass
class Case:
    name: str
    run: Callable
    cost: float                     # seconds, for scheduling until a measured time exists
    paths: tuple = ()               # source globs (fnmatch, repository-relative) it covers
    args: tuple = ()                # extra engine options
    logo: bool = False              # start through the caution screen and logos
    seed: str = "gallery"           # "gallery": all-CG save; "winsave": the Windows save
    maxfps: int = 8                 # presents/s (-maxfps; Xvfb has no vsync); 0 where the video/audio sync is measured
    suites: tuple = field(default=())


EMOTE = ("modules/emote/*", "games/lllj/tests/emote_test.ks")
MOVIE = ("modules/movie-ffmpeg/*",)
ZH = ("modules/xpck-patch/*", "games/lllj/zh-patch.toml", "games/lllj/game.cpp", "modules/toml/*")
SD = [n for n in sc.EMOTE_EVS if n.startswith("sd")]
TITLE_BG = [n for n in sc.EMOTE_EVS if n.startswith("title_bg")]
TRANSITIONS = [t[0] for t in sc.TRANSITIONS]


def p(fn, *args):
    return lambda session: fn(session, *args)


CASES = [
    Case("startup", sc.startup, 16, EMOTE + ("games/lllj/prelude/*", "run.sh", "games/lllj/game.conf"), logo=True, suites=("smoke", "emote")),
    Case("dialogue_backlog", sc.dialogue_backlog, 6, ("modules/textrender/*",), suites=("smoke",)),
    Case("save_pages", sc.save_pages, 7, (), suites=("smoke",)),
    Case("save_load", sc.save_load, 8, ("modules/packinone/*", "games/lllj/import_saves.py"), suites=("smoke",)),
    Case("gallery_cg", p(sc.gallery, ["cgmode"]), 8, (), suites=("smoke",)),
    Case("gallery_modes", p(sc.gallery, ["scenemode", "exchview"]), 6, (), suites=("smoke",)),
    Case("settings_a", p(sc.settings, 0, 4), 7, ("modules/msdfrender/*", "modules/textrender/*"), suites=("smoke",)),
    Case("settings_b", p(sc.settings, 5, 9), 7, ("modules/msdfrender/*", "modules/textrender/*"), suites=("smoke",)),
    Case("controls", sc.controls, 9, ("modules/win32/*", "tools/dev/click.sh"), suites=("smoke",)),
    Case("jp", sc.jp, 9, ("games/lllj/game.cpp", "games/lllj/game.conf"), args=("-lllj_zh=no",), suites=("smoke",)),
    Case("emote_sd_a", p(sc.emote_motions, SD[:8]), 8, EMOTE, suites=("emote",)),
    Case("emote_sd_b", p(sc.emote_motions, SD[8:16]), 8, EMOTE, suites=("emote",)),
    Case("emote_sd_c", p(sc.emote_motions, SD[16:24]), 8, EMOTE, suites=("emote",)),
    Case("emote_sd_d", p(sc.emote_motions, SD[24:]), 8, EMOTE, suites=("emote",)),
    Case("emote_title_bg", p(sc.emote_motions, TITLE_BG), 9, EMOTE, suites=("emote",)),
    Case("emote_eyecatch", sc.emote_eyecatch, 8, EMOTE, suites=("emote",)),
    Case("movie_op", p(sc.movies, ["op"]), 9, MOVIE, maxfps=30, suites=("movie",)),
    Case("movie_skip", p(sc.movies, ["ed_anj", "ed_tuk"]), 9, MOVIE, maxfps=30, suites=("movie",)),
    Case("movie_end_a", p(sc.movies, ["ed_ena", "ed_rir"]), 10, MOVIE, maxfps=30, suites=("movie",)),
    Case("movie_end_b", p(sc.movies, ["ed_mik", "ed_nay"]), 10, MOVIE, maxfps=30, suites=("movie",)),
    Case("movie_end_c", p(sc.movies, ["ed_gra"]), 7, MOVIE, maxfps=30, suites=("movie",)),
    Case("audio_voice", sc.audio_voice, 6, ("modules/audio/*", "engine/common/sound/*"), suites=("audio",)),
    Case("audio_effects", sc.audio_effects, 10, ("modules/audio/*", "engine/common/sound/*"), suites=("audio",)),
    Case("audio_mtrack", sc.audio_mtrack, 10, ("modules/audio/*", "engine/common/sound/*"), suites=("audio",)),
    Case("extras_static", sc.extras_static, 4, ("modules/packinone/*", "modules/layerstwcopy/*", "modules/msdfrender/*"), suites=("extras",)),
    Case("extras_ui", sc.extras_ui, 8, ("modules/layerexdraw/*", "modules/msdfrender/*"), suites=("extras",)),
    Case("extras_wipe", sc.extras_wipe, 7, ("modules/layerstwcopy/*",), suites=("extras",)),
    Case("extras_trans_a", p(sc.extras_transitions, TRANSITIONS[:5]), 9, ("games/lllj/tests/emote_test.ks",), suites=("extras",)),
    Case("extras_trans_b", p(sc.extras_transitions, TRANSITIONS[5:]), 9, ("games/lllj/tests/emote_test.ks",), suites=("extras",)),
    Case("zh_lang_ui", sc.zh_lang_ui, 9, ZH + ("modules/webp/*", "games/lllj/prelude/*"), suites=("zh",)),
    Case("zh_dialogue", sc.zh_dialogue, 10, ZH + ("games/lllj/prelude/*", "modules/textrender/*"), suites=("zh",)),
    Case("zh_subtitles", sc.zh_subtitles, 10, ZH + MOVIE, maxfps=30, suites=("zh",)),
    Case("zh_mv1", sc.zh_mv1, 7, ZH + MOVIE, maxfps=30, suites=("zh",)),
    Case("winsave", sc.winsave, 7, ("games/lllj/import_saves.py", "tools/formats/*", "modules/packinone/*"), seed="winsave"),
]
BY_NAME = {c.name: c for c in CASES}

# Changes that can affect everything select this set instead of every case. The full
# regress (`run.py all`) is never chosen implicitly.
CORE = ("dialogue_backlog", "save_load", "controls", "extras_static", "zh_lang_ui")
CORE_PATHS = ("engine/*", "modules/compat-core/*", "modules/diagnostics/*", "modules/third_party/*",
              "modules/compat.cmake", "CMakeLists.txt", "CMakePresets.json", "cmake/*", "vendor/*",
              "games/lllj/CMakeLists.txt", "games/lllj/game.cpp", "games/lllj/prelude/*", "run.sh", "build.sh",
              "tools/dev/*", "games/lllj/tests/scenarios.py", "games/lllj/tests/scenarios.tjs",
              "games/lllj/tests/cases.py", "games/lllj/tests/run.py")


def run_case(out, name, engine_args=()):
    case = BY_NAME[name]
    seed = {"gallery_seed": True} if case.seed == "gallery" else {"save_seed": sc.WINSAVE_SOURCE}
    # Many instances share the machine: cap the presents, and a draw thread pool of 2 instead
    # of one per CPU (an idle pool of 20 threads costs ~20% more CPU, no faster).
    fps = (f"-maxfps={case.maxfps}",) if case.maxfps else ()
    session = sc.Session(out, (*fps, "-drawthread=2", *case.args, *engine_args), logo=case.logo, **seed)
    try:
        if not case.logo:
            session.start()
        case.run(session)
        known = sc.check_log(session.out / "game.log")
        seen = sc.stubs_seen(session.out / "game.log")
        session.write_report(passed=True, known_exceptions={"|".join(k): sc.KNOWN_CAUGHT[k] for k in known},
                             win32_stubs={n: sc.WIN32_STUBS[n] for n in seen})
        print(f"PASS: {session.out}/report.json")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[2] not in BY_NAME:
        sys.exit(f"usage: cases.py OUT NAME [engine args...]\ncases: {' '.join(BY_NAME)}")
    run_case(sys.argv[1], sys.argv[2], sys.argv[3:])
