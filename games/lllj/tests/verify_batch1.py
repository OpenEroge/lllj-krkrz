#!/usr/bin/env python3
"""Storage and import regression tests through the real engine REPL."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
from scenarios import Session
from import_saves import import_saves


def hx(name):
    return hashlib.blake2s((name.lower() + "xp3hnp").encode("utf-16le")).hexdigest().upper()


def verify(out):
    session = Session(out, ("-nostartup", "-storagetrace=yes"))
    try:
        session.wait('typeof global.CompatNative == "Object"', 30)
        q = lambda x: json.dumps(str(x), ensure_ascii=False)
        def path(name):
            return json.loads(session.ev(f"Storages.getPlacedPath({q(name)})"))
        def add(directory):
            session.ev(f"Storages.addAutoPath({q(str(directory) + '/')})")
        def remove(directory):
            session.ev(f"Storages.removeAutoPath({q(str(directory) + '/')})")
        def stats():
            return session.obj("CompatNative.storageStats()")
        fixtures = session.out / "fixtures"
        a, b, external = (fixtures / x for x in ("a", "b", "outside"))
        for directory in (a, b, external):
            directory.mkdir(parents=True)
        for directory in (a, b):
            (directory / "batch1-collision.txt").write_text(directory.name)
        probe = fixtures / "trace_probe.tjs"
        probe.write_text("function llljPathTraceProbe(){Storages.addAutoPath(" + q(str(a) + "/") + ");}\nllljPathTraceProbe();\n")
        session.ev(f"Scripts.execStorage({q(probe)})")
        trace = (session.out / "game.log").read_text()
        assert "llljPathTraceProbe" in trace and "trace_probe.tjs" in trace
        for leaf in ("ena_001_0001.ogg", "混合_A😀.func", "x" * 80 + ".ogg"):
            assert json.loads(session.ev(f"CompatNative.hashFilename({q(leaf)}, \"xp3hnp\")")) == hx(leaf)
        hashed_leaf = "batch1-hashed.ogg"
        (a / hx(hashed_leaf)).write_bytes(b"hashed fixture")
        add(a)
        assert path("batch1-collision.txt").endswith("/a/batch1-collision.txt")
        assert path(hashed_leaf).endswith("/a/" + hx(hashed_leaf))
        assert path(a / hashed_leaf).endswith("/a/" + hx(hashed_leaf))
        add(b)
        assert path("batch1-collision.txt").endswith("/b/batch1-collision.txt")
        before = stats()
        add(a)  # idempotent: does not reorder or invalidate
        remove(fixtures / "not-mounted")
        assert path("batch1-collision.txt").endswith("/b/batch1-collision.txt")
        assert stats()["rebuilds"] == before["rebuilds"]
        remove(b)
        assert path("batch1-collision.txt").endswith("/a/batch1-collision.txt")
        assert path("batch1-created.pbd") == ""
        session.ev(f"CompatNative.saveDataPack({q(a / 'batch1-created.pbd')}, 42)")
        assert path("batch1-created.pbd").endswith("/a/batch1-created.pbd")
        assert session.ev('CompatNative.loadDataPack("batch1-created.pbd")') == "42"
        before = stats()
        for i in range(3):
            session.ev(f"CompatNative.saveDataPack({q(external / 'save.pbd')},{i})")
            session.ev("System.doCompact()")
            assert path("batch1-collision.txt").endswith("/a/batch1-collision.txt")
        assert stats()["scans"] == before["scans"]
        assert stats()["rebuilds"] == before["rebuilds"]
        (a / "batch1-external.txt").write_text("external addition")
        session.ev("Storages.clearStorageCaches()")
        assert path("batch1-external.txt").endswith("/a/batch1-external.txt")
        assert stats()["scans"] > before["scans"]
        # Named resources take precedence over a hashed version even when the
        # latter belongs to a later auto path.
        (b / hashed_leaf).write_text("named fixture")
        add(b)
        add(external)
        (external / hx(hashed_leaf)).write_text("later hashed fixture")
        session.ev("Storages.clearStorageCaches()")
        assert path(hashed_leaf).endswith("/b/" + hashed_leaf)
        # Exercise XP3 search precedence, the same mechanism used by patch2.
        for directory in (a, b):
            subprocess.run([sys.executable, "-I", str(ROOT / "tools/formats/xp3plain.py"), str(directory), str(fixtures / (directory.name + ".xp3"))], check=True, stdout=subprocess.DEVNULL)
        for directory in (a, b, external):
            remove(directory)
        for name in ("a", "b"):
            session.ev(f"Storages.addAutoPath({q(str(fixtures / (name + '.xp3')) + '>')})")
        assert "/b.xp3>" in path("batch1-collision.txt")
        session.ev(f"Storages.removeAutoPath({q(str(fixtures / 'b.xp3') + '>')})")
        assert "/a.xp3>" in path("batch1-collision.txt")
        # Windows JPEG containers split a pack over multiple byte-stuffed APP11
        # segments. Make the payload cross 16KiB and contain many FF bytes.
        plain = fixtures / "container-pack.pbd"
        session.ev(f"CompatNative.saveDataPack({q(plain)},%[text:(\"ÿ漢😀\").repeat(6000)])")
        pack = plain.read_bytes()
        jpg = bytearray(b"\xff\xd8")
        for offset in range(0, len(pack), 16384):
            data = pack[offset:offset + 16384].replace(b"\xff", b"\xff\x00")
            jpg.extend(b"\xff\xeb" + (len(data) + 2).to_bytes(2, "big") + data)
        jpg.extend(b"\xff\xd9")
        container = fixtures / "container.jpg"
        container.write_bytes(jpg)
        assert session.ev(f"CompatNative.loadDataPack({q(container)}).text == (\"ÿ漢😀\").repeat(6000)") == "1"
        malformed = fixtures / "truncated.jpg"
        malformed.write_bytes(jpg[:100])
        try:
            session.ev(f"CompatNative.loadDataPack({q(malformed)})")
        except RuntimeError as error:
            assert "JPEG segment truncated" in str(error), error
        else:
            raise AssertionError("Truncated JPEG save was accepted")
        # Explicit import never replaces existing files and leaves its source.
        src, dst = fixtures / "windows-saves", fixtures / "imported-saves"
        src.mkdir(); dst.mkdir()
        (src / "datasu.ksd").write_bytes(b"source")
        (src / "data.jpg").write_bytes(b"thumbnail")
        (dst / "datasu.ksd").write_bytes(b"existing")
        imported = import_saves(src, dst)
        assert imported == {"copied": ["data.jpg"], "skipped": ["datasu.ksd"]}
        assert (dst / "datasu.ksd").read_bytes() == b"existing"
        assert (src / "datasu.ksd").read_bytes() == b"source"
        session.write_report(passed=True, storage_stats=stats(), cases=["opt-in TJS caller stacks", "Hxv4 vectors", "directory and XP3 priority", "duplicate paths", "negative cache and writes", "compaction", "explicit refresh", "segmented byte-stuffed JPEG data packs", "truncated save rejection", "non-overwriting import"])
        print(f"PASS: {session.out}/report.json")
    finally:
        session.close()


if __name__ == "__main__":
    verify(sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="lllj-storage-"))
