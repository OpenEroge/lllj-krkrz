#!/usr/bin/env python3
"""Read the supplied Windows continue save through the game's real load path."""
import hashlib
import json
from pathlib import Path
import sys
import shutil

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from scenarios import Session, advance, check_log
from pbd_decode import decode


def verify(out, source):
    source = Path(source).resolve()
    original = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}
    expected = decode((source / "data_continue.jpg").read_bytes())["core"]["scenePlayer"]
    session = Session(out, save_seed=source)
    try:
        session.boot()
        session.finish_startup()
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
        state = session.step("windows_continue_loaded", 2)
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
        known = check_log(session.out / "game.log")
        assert original == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}
        session.write_report(passed=True, save_summary=summary, text=text, expected_position={"scene": expected["scene"], "point": expected["point"]}, thumbnail=dimensions, known_exceptions=known)
        print(f"PASS: {session.out}/report.json")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    verify(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ROOT.parent / "limelight/savedata_zh")
