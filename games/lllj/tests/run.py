#!/usr/bin/env python3
"""Run scenario cases in parallel, each in its own engine, Xvfb and output directory.

  run.py --changed                 cases covering what changed since HEAD (work tree + untracked)
  run.py --changed=REV             ... since REV
  run.py emote_sd_a zh             named cases and/or suites
  run.py all                       the full regress (also games/lllj/tests/regress.sh)
options: -o OUT (default artifacts/t-<time>), -j N (default 4/5 of the CPUs), --engine-arg=OPT, --list
Results: OUT/summary.txt, OUT/<case>/ (report.json, game.log, screenshots), OUT/<case>.out."""
import argparse
import fnmatch
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parents[2]
sys.path.insert(0, str(TESTS))
from cases import CASES, BY_NAME, CORE, CORE_PATHS  # noqa: E402

TIMINGS = ROOT / "artifacts/.case-timings.json"  # measured seconds per case, for scheduling
CASE_TIMEOUT = 180


def suites():
    groups = {"all": [c.name for c in CASES], "core": list(CORE)}
    for case in CASES:
        for suite in case.suites:
            groups.setdefault(suite, []).append(case.name)
    return groups


def changed_files(rev):
    """Changed paths since rev, descending into submodules (engine/ is one): there the
    parent only reports the submodule's own path."""
    def git(cwd, *args):
        out = subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout
        return [f for f in out.split("\n") if f]
    submodules = {line[1:].split()[1] for line in git(ROOT, "submodule", "status")}  # "[ +-U]sha path (describe)"
    files = set()
    for f in git(ROOT, "diff", "--name-only", rev) + git(ROOT, "ls-files", "--others", "--exclude-standard"):
        if f not in submodules:
            files.add(f)
            continue
        # the submodule commit the parent recorded at rev, else its own HEAD
        recorded = git(ROOT, "ls-tree", rev, f)
        base = recorded[0].split()[2] if recorded else "HEAD"
        sub = ROOT / f
        files.update(f"{f}/{g}" for g in git(sub, "diff", "--name-only", base) + git(sub, "ls-files", "--others", "--exclude-standard"))
    return sorted(files)


def select_changed(files):
    chosen, reasons = [], {}
    for f in files:
        hits = [c.name for c in CASES if any(fnmatch.fnmatch(f, g) for g in c.paths)]
        if any(fnmatch.fnmatch(f, g) for g in CORE_PATHS):
            hits += CORE
        for name in hits:
            reasons.setdefault(name, f)
        chosen += hits
    return [c.name for c in CASES if c.name in chosen], reasons


def stage():
    """Copy the engine into the run directory once, so parallel instances need not."""
    env = dict(os.environ, KRKRZ_STAGE="only")
    subprocess.run([str(ROOT / "run.sh")], cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", help="case or suite names")
    parser.add_argument("--changed", nargs="?", const="HEAD", metavar="REV")
    parser.add_argument("-o", "--out", type=Path)
    parser.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) * 4 // 5))
    parser.add_argument("--engine-arg", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    groups = suites()
    if args.list:
        for case in CASES:
            print(f"{case.name:18} {','.join(case.suites)}")
        print("suites:", " ".join(groups))
        return 0

    names, reasons = [], {}
    if args.changed:
        files = changed_files(args.changed)
        names, reasons = select_changed(files)
        if not names:
            print(f"--changed: no case covers the {len(files)} changed file(s)")
            return 0
    for name in args.names:
        if name not in groups and name not in BY_NAME:
            parser.error(f"unknown case or suite: {name} (see --list)")
        names += groups.get(name, [name])
    names = list(dict.fromkeys(names))
    if not names:
        parser.error("name cases/suites, 'all', or use --changed")

    out = (args.out or ROOT / "artifacts" / time.strftime("t-%Y%m%d-%H%M%S")).resolve()
    if out.exists() and any(out.iterdir()):
        parser.error(f"choose a fresh output directory: {out}")
    out.mkdir(parents=True, exist_ok=True)
    try:
        timings = json.loads(TIMINGS.read_text())
    except (OSError, ValueError):
        timings = {}
    names.sort(key=lambda n: -timings.get(n, BY_NAME[n].cost))  # longest first
    for name in names:
        if name in reasons:
            print(f"  {name:18} <- {reasons[name]}")

    stage()
    env = dict(os.environ, KRKRZ_STAGE="skip")
    env.setdefault("LP_NUM_THREADS", "1")  # llvmpipe: keep parallel instances from starving each other
    # Xvfb has no vsync: an idle instance would iterate and present ~150 times/s on ~2 cores.
    # SDL's main callback rate caps the iterations (the REPL answers one request per
    # iteration); each case's -maxfps caps the presents, which is where llvmpipe spends.
    env.setdefault("SDL_MAIN_CALLBACK_RATE", "60")
    began = time.monotonic()
    pending, running, results = list(names), {}, {}
    while pending or running:
        while pending and len(running) < args.jobs:
            name = pending.pop(0)
            log = (out / f"{name}.out").open("w")
            proc = subprocess.Popen([sys.executable, "-I", str(TESTS / "cases.py"), str(out / name), name, *args.engine_arg],
                                    cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            running[name] = (proc, log, time.monotonic())
        time.sleep(0.05)
        for name, (proc, log, started) in list(running.items()):
            elapsed = time.monotonic() - started
            if elapsed > CASE_TIMEOUT:
                os.killpg(proc.pid, signal.SIGKILL)
            # wait4: the CPU time of the case's whole process tree (engine, Xvfb, driver)
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid == 0:
                continue
            proc.returncode = os.waitstatus_to_exitcode(status)
            log.close()
            del running[name]
            ok = proc.returncode == 0
            cpu = usage.ru_utime + usage.ru_stime
            results[name] = (ok, elapsed, cpu)
            if ok:
                timings[name] = round(elapsed, 1)
            print(f"{'PASS' if ok else 'FAIL'} {name:18} ({elapsed:.1f}s, cpu {cpu:.1f}s)", flush=True)
            if not ok:
                lines = (out / f"{name}.out").read_text(errors="replace").splitlines()
                print("\n".join("    " + line for line in lines[-30:]), flush=True)
    wall = time.monotonic() - began
    failed = [n for n, (ok, _, _) in results.items() if not ok]
    summary = [f"{'PASS' if results[n][0] else 'FAIL'} {n:18} ({results[n][1]:.1f}s, cpu {results[n][2]:.1f}s)" for n in names]
    cpu = sum(r[2] for r in results.values())
    summary.append(f"{len(names) - len(failed)}/{len(names)} passed in {wall:.1f}s wall, {cpu:.0f}s cpu (-j {args.jobs})")
    (out / "summary.txt").write_text("\n".join(summary) + "\n")
    print(summary[-1] + (f"; failed: {' '.join(failed)}" if failed else "") + f"\n{out}")
    try:
        TIMINGS.parent.mkdir(parents=True, exist_ok=True)
        TIMINGS.write_text(json.dumps(timings, indent=1, sort_keys=True))
    except OSError:
        pass
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
