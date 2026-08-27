#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ENTRY = re.compile(r"(?m)^###\s+#(?P<number>\d+)\s+—\s+(?P<title>.+)$")
REFERENCE = re.compile(
    r"(?ms)^\*\*Hanabi reference\.\*\*\s*(?P<text>.*?)(?=^-?\s*\*\*[^\n]+\.\*\*|^###\s+#|\Z)"
)
BACKTICK = re.compile(r"`([^`]+)`")
CLASS_WORKAROUND = re.compile(
    r"(?im)(?:^CLASS:\s*WORKAROUND\b|^\*\*Class\.\*\*\s*`?WORKAROUND`?\b)"
)
SHIPPED_WORKAROUND = re.compile(
    r"(?is)(?:\bapp-side workaround\b|\bproof patch\b|\bspike,? not shipped\b|"
    r"\bhanabi(?:'s)?\s+(?:current\s+)?(?:workaround|ships|shipped|implements|implemented|uses|used|built)\b|"
    r"\b(?:worked around|workaround|shipped|implemented)\b.{0,80}\b(?:in|by)\s+hanabi\b)"
)
PATH_ROOTS = {
    "src",
    "tests",
    "scripts",
    "docs",
    "vendor_patches",
    "vendor",
    "resources",
    "tools",
    "mock",
    "hanabi",
}


@dataclass(frozen=True)
class Entry:
    number: int
    title: str
    body: str


def parse_entries(text: str) -> list[Entry]:
    matches = list(ENTRY.finditer(text))
    return [
        Entry(
            number=int(match.group("number")),
            title=match.group("title"),
            body=text[
                match.end() : matches[index + 1].start()
                if index + 1 < len(matches)
                else len(text)
            ],
        )
        for index, match in enumerate(matches)
    ]


def needs_reference(entry: Entry) -> bool:
    return (
        "**The workaround.**" in entry.body
        or CLASS_WORKAROUND.search(entry.body) is not None
        or SHIPPED_WORKAROUND.search(entry.body) is not None
    )


def reference_text(entry: Entry) -> str | None:
    match = REFERENCE.search(entry.body)
    return match.group("text").strip() if match else None


def repository_path(token: str) -> str | None:
    candidate = token.strip()
    if not candidate or any(char.isspace() for char in candidate):
        return None
    candidate = candidate.split("::", 1)[0]
    if ":" in candidate:
        candidate = candidate.split(":", 1)[0]
    candidate = candidate.rstrip(".,;)")
    first = candidate.split("/", 1)[0]
    if first in PATH_ROOTS and "/" in candidate:
        return candidate
    if "/" not in candidate and candidate in {
        "makefile",
        "afterhours_gaps.md",
        "afterhours_gaps_index.md",
    }:
        return candidate
    return None


def check_document(text: str, root: Path) -> list[str]:
    problems = []
    entries = parse_entries(text)
    if not entries:
        return ["no numbered gap entries found"]
    for entry in entries:
        reference = reference_text(entry)
        if needs_reference(entry) and reference is None:
            problems.append(
                f"#{entry.number} {entry.title}: workaround claim has no "
                "`**Hanabi reference.**` subsection"
            )
            continue
        if reference is None:
            continue
        if re.match(r"(?i)^None\s+—\s+", reference):
            continue
        paths = []
        for token in BACKTICK.findall(reference):
            path = repository_path(token)
            if path is not None:
                paths.append(path)
        if not paths:
            problems.append(
                f"#{entry.number} {entry.title}: Hanabi reference names no "
                "backticked repository path"
            )
            continue
        for path in sorted(set(paths)):
            if not (root / path).is_file():
                problems.append(
                    f"#{entry.number} {entry.title}: stale Hanabi reference `{path}`"
                )
    return problems


def selftest() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "src").mkdir()
        (root / "src" / "demo.h").write_text("struct Demo {};\n")
        cases = [
            (
                "workaround missing",
                "### #1 — Missing\n\n**The workaround.** Ship it.\n",
                False,
            ),
            (
                "class missing",
                "### #2 — Missing\n\nCLASS: WORKAROUND\n",
                False,
            ),
            (
                "shipped missing",
                "### #3 — Missing\n\nHanabi ships a local adapter.\n",
                False,
            ),
            (
                "no workaround",
                "### #4 — None\n\n**The workaround.** None app-side.\n\n"
                "**Hanabi reference.** None — no app-side workaround is implemented.\n",
                True,
            ),
            (
                "stable symbol",
                "### #5 — Present\n\n**The workaround.** Adapter.\n\n"
                "**Hanabi reference.** `src/demo.h::Demo` — the adapter.\n",
                True,
            ),
            (
                "stale path",
                "### #6 — Stale\n\nCLASS: WORKAROUND\n\n"
                "**Hanabi reference.** `src/gone.h:Gone` — old code.\n",
                False,
            ),
        ]
        failed = []
        for name, text, should_pass in cases:
            passed = not check_document(text, root)
            if passed != should_pass:
                failed.append(name)
        if failed:
            print("check_gap_references selftest: FAIL: " + ", ".join(failed))
            return 1
    print(f"check_gap_references selftest: {len(cases)} cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--document", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    root = (args.root or Path(__file__).resolve().parent.parent).resolve()
    document = (args.document or root / "afterhours_gaps.md").resolve()
    problems = check_document(document.read_text(), root)
    if problems:
        print("check_gap_references: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    count = len(parse_entries(document.read_text()))
    print(f"check_gap_references: {count} entries, all workaround references present and live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
