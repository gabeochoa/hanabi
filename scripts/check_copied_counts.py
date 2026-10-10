#!/usr/bin/env python3
"""Fail if build.zig/makefile copy a test-executable count by hand.

The test list in build.zig is the one owner. Hand-copied counts drifted
twice: docs/COMMIT_AUDIT.md (D2) corrected 58 copied claims, and the
makefile said 75 while build.zig said 72. Fix: say what runs, not how many.
"""
import pathlib, re, sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
PAT = re.compile(r"test executables \(\d+\)|\b\d+\s+test executables\b|The \d+ less\b")
bad=[]
for rel in ("build.zig","makefile"):
    for i,line in enumerate((ROOT/rel).read_text().splitlines(),1):
        if PAT.search(line) and "check_copied_counts" not in line:
            bad.append(f"{rel}:{i}: copied count: {line.strip()} -- fix: delete the number; build.zig's test list is the owner")
print("\n".join(bad) if bad else "copied-count check ok: no hand-copied test counts")
sys.exit(1 if bad else 0)
