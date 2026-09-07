#!/usr/bin/env python3
"""Two source words this project does not use, kept out by a gate.

Both are spelled here only as assembled fragments, so this file does not itself
contain either word -- a checker that has to write the thing it forbids is a
checker that trips itself, and every future grep for the word finds the gate
rather than the offence.

Scope is what THIS branch adds. The words already appear in files that predate
it -- prose about where a rule came from -- and rewriting that history would be
unrelated churn in a change about settings. So the gate reads the diff against
the base and fails on an ADDED line, which is the thing a contributor controls.
Run with --tree to scan every file instead, which is how the pre-existing
occurrences can be counted without failing the build.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Assembled at runtime; see the module docstring.
FORBIDDEN = ("Puf" + "fin", "Me" + "ta")

SKIP_DIRS = {
    ".git", "vendor", "vendor_patches", "output", "test-failures",
    "node_modules", "__pycache__",
}
SKIP_NAMES = {os.path.basename(__file__)}
TEXT_SUFFIXES = {
    ".h", ".hpp", ".c", ".cc", ".cpp", ".m", ".mm", ".py", ".sh", ".e2e",
    ".md", ".json", ".txt", ".plist",
}

# `metal`, `metadata` and friends are ordinary words that merely start the same
# way, and the platform's own symbols use them. Only a standalone occurrence is
# the project word, so the match is anchored on word boundaries.
PATTERNS = [(word, re.compile(r"\b" + re.escape(word) + r"\b")) for word in FORBIDDEN]


def candidates():
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name in SKIP_NAMES:
                continue
            path = os.path.join(base, name)
            suffix = os.path.splitext(name)[1]
            if suffix not in TEXT_SUFFIXES and name != "makefile":
                continue
            yield path


def added_lines():
    """(path, line-number-ish, text) for every line this branch adds."""
    base = subprocess.run(
        ["git", "merge-base", "HEAD", "origin/main"],
        cwd=ROOT, capture_output=True, text=True)
    ref = base.stdout.strip() or "origin/main"
    diff = subprocess.run(["git", "diff", "-U0", ref, "--"],
                          cwd=ROOT, capture_output=True, text=True)
    path = None
    for line in diff.stdout.splitlines():
        if line.startswith("+++ b/"):
            path = line[6:]
        elif line.startswith("+") and not line.startswith("+++"):
            if path is not None:
                yield path, 0, line[1:]
    untracked = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard"],
        cwd=ROOT, capture_output=True, text=True)
    for rel in untracked.stdout.split():
        full = os.path.join(ROOT, rel)
        if os.path.splitext(rel)[1] not in TEXT_SUFFIXES:
            continue
        try:
            with open(full, encoding="utf-8", errors="strict") as handle:
                for lineno, text in enumerate(handle, 1):
                    yield rel, lineno, text
        except (UnicodeDecodeError, OSError):
            continue


def scan_delta():
    hits = []
    for path, lineno, text in added_lines():
        if os.path.basename(path) in SKIP_NAMES:
            continue
        if any(part in SKIP_DIRS for part in path.split(os.sep)):
            continue
        for word, pattern in PATTERNS:
            if pattern.search(text):
                hits.append((path, lineno, word))
    if hits:
        print("check_vocabulary: FAIL", file=sys.stderr)
        for path, lineno, word in hits[:40]:
            where = f"{path}:{lineno}" if lineno else path
            print(f"  {where} adds a prohibited project name "
                  f"({len(word)} letters, starts {word[0]!r})", file=sys.stderr)
        print(f"  {len(hits)} added occurrence(s)", file=sys.stderr)
        return 1
    print("check_vocabulary: no prohibited project names added")
    return 0


def scan_tree():
    hits = []
    scanned = 0
    for path in candidates():
        try:
            with open(path, encoding="utf-8", errors="strict") as handle:
                body = handle.read()
        except (UnicodeDecodeError, OSError):
            continue
        scanned += 1
        for lineno, line in enumerate(body.splitlines(), 1):
            for word, pattern in PATTERNS:
                if pattern.search(line):
                    hits.append((os.path.relpath(path, ROOT), lineno, word))

    print(f"check_vocabulary: {scanned} files scanned, "
          f"{len(hits)} pre-existing occurrence(s)")
    return 0


if __name__ == "__main__":
    if "--tree" in sys.argv:
        sys.exit(scan_tree())
    sys.exit(scan_delta())
