#!/usr/bin/env python3
"""Verify real startup mount order and bytes of all batch 1 resources."""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]  # repository root
TESTS = ROOT / "games/lllj/tests"
for _p in (TESTS, ROOT / "games/lllj", ROOT / "tools/dev", ROOT / "tools/formats"):
    sys.path.insert(0, str(_p))
from scenarios import Session, check_log


def verify(out):
    session = Session(out)
    try:
        session.boot()
        result = (ROOT / "game/voice").resolve().parent
        replacements = sorted((result / "unencrypted").glob("*.pimg"))
        assert len(replacements) == 53
        records = []
        for file in replacements:
            name = json.dumps(file.name)
            resolved = json.loads(session.ev(f"Storages.getPlacedPath({name})"))
            assert "/patch2.xp3>" in resolved, resolved
            # Independent digest of the readonly source versus the engine's
            # stream through the mounted XP3, without materializing a large
            # octet/string in the REPL response.
            expected = hashlib.md5(file.read_bytes()).hexdigest()
            actual = json.loads(session.ev(f"Storages.getMD5HashString({name})"))
            assert actual.lower() == expected, file.name
            records.append({"name": file.name, "resolved": resolved, "md5": expected})
        hashes = []
        for archive in ("voice", "voice2", "uipsd"):
            for file in sorted((result / archive).rglob("*")):
                if not file.is_file() or not re.fullmatch("[A-F0-9]{64}", file.name) or file.name.startswith("2EA4AAEC"):
                    continue
                expected = hashlib.md5(file.read_bytes()).hexdigest()
                name = json.dumps(str(file), ensure_ascii=False)
                actual = json.loads(session.ev(f"Storages.getMD5HashString({name})"))
                assert actual.lower() == expected, file.name
                hashes.append({"file": str(file.relative_to(result)), "md5": expected})
        assert len(hashes) == 16
        session.write_report(passed=True, replacements=records, unnamed_files=hashes,
                             unnamed_note="Original names remain unknown; fallback algorithm is verified against independent vectors and synthetic named requests in verify_batch1.py.",
                             known_exceptions=check_log(session.out / "game.log"))
        print(f"PASS: {session.out}/report.json (53 replacements, 16 unnamed files)")
    except Exception as error:
        session.write_report(passed=False, error=str(error))
        raise
    finally:
        session.close()


if __name__ == "__main__":
    verify(sys.argv[1])
