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


def candidates(root=ROOT):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name in SKIP_NAMES:
                continue
            path = os.path.join(base, name)
            suffix = os.path.splitext(name)[1]
            if suffix not in TEXT_SUFFIXES and name != "makefile":
                continue
            yield path


class GitUnavailable(Exception):
    pass


def git(args, root):
    try:
        done = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)
    except OSError as exc:
        raise GitUnavailable(f"git {args[0]}: cannot run git ({exc})") from exc
    if done.returncode != 0:
        detail = done.stderr.strip().splitlines()
        why = detail[-1] if detail else f"exit {done.returncode}"
        raise GitUnavailable(f"git {' '.join(args)}: {why}")
    return done.stdout


def require_work_tree(root):
    inside = git(["rev-parse", "--is-inside-work-tree"], root).strip()
    if inside != "true":
        raise GitUnavailable(f"git rev-parse --is-inside-work-tree: {inside!r} is not a work tree")
    head = git(["rev-parse", "--verify", "--quiet", "HEAD"], root).strip()
    if not re.fullmatch(r"[0-9a-f]{40,64}", head):
        raise GitUnavailable(f"git rev-parse HEAD: {head!r} is not a commit")
    return head


def merge_base(root, base_ref):
    git(["rev-parse", "--verify", "--quiet", base_ref + "^{commit}"], root)
    base = git(["merge-base", "HEAD", base_ref], root).strip()
    if not re.fullmatch(r"[0-9a-f]{40,64}", base):
        raise GitUnavailable(f"git merge-base HEAD {base_ref}: {base!r} is not a commit")
    return base


def added_lines(root=ROOT, base_ref="origin/main"):
    require_work_tree(root)
    ref = merge_base(root, base_ref)
    diff = git(["diff", "-U0", ref, "--"], root)
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            path = line[6:]
        elif line.startswith("+++ "):
            path = None
        elif line.startswith("+") and not line.startswith("+++"):
            if path is not None:
                yield path, 0, line[1:]
    untracked = git(["ls-files", "--others", "--exclude-standard", "-z"], root)
    for rel in untracked.split("\0"):
        if not rel:
            continue
        full = os.path.join(root, rel)
        if os.path.splitext(rel)[1] not in TEXT_SUFFIXES:
            continue
        try:
            with open(full, encoding="utf-8", errors="strict") as handle:
                for lineno, text in enumerate(handle, 1):
                    yield rel, lineno, text
        except (UnicodeDecodeError, OSError):
            continue


def scan_delta(root=ROOT, base_ref="origin/main"):
    hits = []
    added = 0
    try:
        for path, lineno, text in added_lines(root, base_ref):
            added += 1
            if os.path.basename(path) in SKIP_NAMES:
                continue
            if any(part in SKIP_DIRS for part in path.split(os.sep)):
                continue
            for word, pattern in PATTERNS:
                if pattern.search(text):
                    hits.append((path, lineno, word))
    except GitUnavailable as exc:
        print("check_vocabulary: NOT SCANNED", file=sys.stderr)
        print(f"  {exc}", file=sys.stderr)
        print(f"  delta mode needs a Git work tree with HEAD and {base_ref}; "
              f"use --tree to scan every file without Git", file=sys.stderr)
        return 2
    if hits:
        print("check_vocabulary: FAIL", file=sys.stderr)
        for path, lineno, word in hits[:40]:
            where = f"{path}:{lineno}" if lineno else path
            print(f"  {where} adds a prohibited project name "
                  f"({len(word)} letters, starts {word[0]!r})", file=sys.stderr)
        print(f"  {len(hits)} added occurrence(s) in {added} added line(s)", file=sys.stderr)
        return 1
    print(f"check_vocabulary: no prohibited project names added ({added} added line(s) scanned)")
    return 0


def scan_tree(root=ROOT):
    hits = []
    scanned = 0
    for path in candidates(root):
        try:
            with open(path, encoding="utf-8", errors="strict") as handle:
                body = handle.read()
        except (UnicodeDecodeError, OSError):
            continue
        scanned += 1
        for lineno, line in enumerate(body.splitlines(), 1):
            for word, pattern in PATTERNS:
                if pattern.search(line):
                    hits.append((os.path.relpath(path, root), lineno, word))

    print(f"check_vocabulary: {scanned} files scanned, "
          f"{len(hits)} pre-existing occurrence(s)")
    return 0


def main(argv):
    if "--tree" in argv:
        return scan_tree()
    return scan_delta()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
