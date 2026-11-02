#!/usr/bin/env python3
"""Explicit, non-overwriting import of Windows or previous native saves."""
import argparse
import os
from pathlib import Path
import shutil


def import_saves(source, target):
    source, target = Path(source).resolve(), Path(target).resolve()
    if not source.is_dir() or source == target or source in target.parents:
        raise ValueError("source must be an existing directory distinct from target")
    target.mkdir(parents=True, exist_ok=True)
    copied, skipped = [], []
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"refusing save symlink: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        dest = target / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open("rb") as src, dest.open("xb") as dst:
                shutil.copyfileobj(src, dst)
        except FileExistsError:
            skipped.append(str(relative))
        else:
            copied.append(str(relative))
    return {"copied": copied, "skipped": skipped}


def main():
    import json
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, default=root.parent / "limelight/savedata_zh")
    parser.add_argument("--target", type=Path, default=Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "limelight_lj")
    args = parser.parse_args()
    print(json.dumps(import_saves(args.source, args.target), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
