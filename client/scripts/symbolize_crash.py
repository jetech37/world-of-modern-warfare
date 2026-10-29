#!/usr/bin/env python3
"""Turn a player's crash report's frames into functions and source lines.

A crash report (aw-admin crashes show <id>) names code as `benilla-mw2.exe+0x1a2b3c`: offsets
into the client exe, which mean something only with that build's PDB. package/publish.py keeps
the PDB of every published client in build/symbols/<version>_<sha12>, where sha12 is the first 12
of the client exe's sha256 (`client exe sha256` in `aw-admin crashes show`).

    python client/scripts/symbolize_crash.py report.txt build/symbols/2026-10-18_1200_9095bdb85930
    python client/scripts/symbolize_crash.py report.txt --sha 9095bdb85930...   # finds the folder

Each benilla-mw2.exe offset in the report (frames, the crash address) goes through
server/contrib/crash/symbolize.py (Windows' dbghelp; nothing to install). Frames in other modules
(system DLLs, the graphics driver) are listed but not resolved. With rustfilt installed
(cargo install rustfilt) the Rust names come out readable.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = os.path.join(REPO, "build", "symbols")
SYMBOLIZE = os.path.join(REPO, "server", "contrib", "crash", "symbolize.py")
EXE = "benilla-mw2.exe"
MANGLED = re.compile(r"(?<=symbol   )(R[A-Za-z0-9_]+)")
FRAME = re.compile(r"\b([A-Za-z0-9_.-]+\.(?:exe|dll))\+0x([0-9a-fA-F]+)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report")
    ap.add_argument("symbols", nargs="?", help="the build's folder under build/symbols")
    ap.add_argument("--sha", help="the client exe's sha256 (or its first 12): finds the folder")
    args = ap.parse_args()

    folder = args.symbols
    if not folder and args.sha:
        sha12 = args.sha.lower()[:12]
        found = [n for n in os.listdir(SYMBOLS) if n.endswith("_" + sha12)] if os.path.isdir(SYMBOLS) else []
        if not found:
            sys.exit(f"no folder for {sha12} in {SYMBOLS}")
        folder = os.path.join(SYMBOLS, found[0])
    if not folder:
        sys.exit("which build? a folder under build/symbols, or --sha")
    exe = os.path.join(folder, EXE)
    if not os.path.exists(exe):
        sys.exit(f"no {EXE} in {folder}")

    text = open(args.report, encoding="utf-8", errors="replace").read()
    ours, others = [], []
    for module, offset in FRAME.findall(text):
        target = ours if module.lower() == EXE else others
        entry = offset.lower() if target is ours else f"{module}+0x{offset}"
        if entry not in target:
            target.append(entry)
    if others:
        print("in other modules (not resolved): " + ", ".join(others))
    if not ours:
        sys.exit(f"no {EXE}+0x... offsets in {args.report}")
    run = subprocess.run([sys.executable, SYMBOLIZE, exe, *("0x" + o for o in ours), "--pdb", folder],
                         capture_output=True, text=True)
    sys.stderr.write(run.stderr)
    print(demangle(run.stdout), end="")
    sys.exit(run.returncode)


def demangle(text: str) -> str:
    """Rust's mangled names (dbghelp drops their leading underscore) as Rust paths, through
    rustfilt when it is installed (cargo install rustfilt)."""
    rustfilt = shutil.which("rustfilt")
    if not rustfilt:
        return text
    marked = MANGLED.sub(lambda m: "_" + m.group(1), text)
    run = subprocess.run([rustfilt], input=marked, capture_output=True, text=True)
    return run.stdout if run.returncode == 0 else text


if __name__ == "__main__":
    main()
