#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys

# Importing this module (its own selftest does) must not leave a .pyc behind
# in the tree.
sys.dont_write_bytecode = True
import tempfile
from dataclasses import dataclass
from pathlib import Path


ENTRY = re.compile(r"(?m)^###\s+#(?P<number>\d+)\s+—\s+(?P<title>.+)$")
# A cross-reference to another gap, anywhere in an entry or the index.
GAP_REF = re.compile(r"#(\d{2,4})\b")
# The closure roster: the only lines allowed to name a removed id. Anchored on
# the roster's own shape so ordinary prose cannot borrow the exemption.
# A row of the index's closure table -- `| #22 | \`a1b9a4b\` | ... |` -- and
# nothing else. Anchored on that exact shape so ordinary prose cannot borrow
# the exemption; a broad phrase match previously let stale text hide here.
CLOSURE_ROSTER = re.compile(r"^\|\s*#\d{2,4}\s*\|\s*`[0-9a-f]{7}`")
# Gap ids that are NOT entries: closed and removed at pin 9ff9079. Citing one
# as if it were live is the failure this catches.
REMOVED = {17, 22, 24, 26, 42, 43, 47, 55, 113, 115, 161, 180, 192,
           200, 211, 220, 231, 351, 352}
# Ids cited by the ledger that have never had an entry OR an index row. Every
# one of these is already dangling at origin/main ad2c798 -- verified against
# that revision, not assumed -- so they are inherited debt rather than anything
# this audit broke. They are listed rather than ignored so the check still
# fails on a NEW dangling id, which is the regression worth catching.
PREEXISTING_DANGLING = {98, 99, 120, 130, 131, 132, 140, 451, 454, 550, 559,
                        # Cited in SOURCE and docs but never an entry or index
                        # row -- at origin/main ad2c798 either, verified there.
                        309, 495, 509}
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


# `path` (`symbol or string`) — the parenthesised quote is a claim about that
# file's CONTENTS, and #374 shipped one naming a function that had been
# renamed. Paths were checked; the claim was not.
SYMBOL_CLAIM = re.compile(r"`(?P<path>[\w./-]+\.(?:h|cpp|mm|py|sh|md|e2e))`"
                          r"\s*\(`(?P<symbol>[^`]{3,120})`\)")


def inherited_symbol_claims(root: Path) -> set[str]:
    path = root / "scripts" / "gap_symbol_baseline.txt"
    if not path.is_file():
        return set()
    return {
        line.strip()
        for line in path.read_text().split("\n")
        if line.strip() and not line.startswith("#")
    }


def check_symbol_claims(text: str, root: Path) -> list[str]:
    """A reference that quotes a symbol must quote one the file contains."""
    problems = []
    inherited = inherited_symbol_claims(root)
    for match in SYMBOL_CLAIM.finditer(text):
        rel, symbol = match.group("path"), match.group("symbol")
        path = root / rel
        if not path.is_file():
            continue  # the path arm already reports a missing file
        try:
            body = path.read_text()
        except (UnicodeDecodeError, OSError):
            continue
        if symbol in body:
            continue
        if f"{rel}|{symbol}" in inherited:
            continue
        line = text.count("\n", 0, match.start()) + 1
        problems.append(
            f"afterhours_gaps.md:{line}: cites `{rel}` (`{symbol}`), but that "
            f"file does not contain it"
        )
    return problems


def check_cross_references(text: str, root: Path) -> list[str]:
    """Every #NNN cited anywhere must resolve to an entry that exists.

    Counting entries proved nothing: the ledger shipped four dangling ids and
    the checker was green, because it only looked at `**Hanabi reference.**`
    paths. A gap id is a reference too.
    """
    problems = []
    live = {entry.number for entry in parse_entries(text)}
    index = root / "afterhours_gaps_index.md"
    sources = [("afterhours_gaps.md", text)]
    index_text = ""
    if index.is_file():
        index_text = index.read_text()
        sources.append(("afterhours_gaps_index.md", index_text))
        # The index carries identifiers that were never given a detailed
        # entry -- it says so itself ("includes 11 index-only identifiers").
        # Those are a real namespace, so a citation of one is not dangling.
        live |= {
            int(m.group(1))
            for m in re.finditer(r"(?m)^\|\s*(\d{2,4})\s*\|", index_text)
        }
    for name, body in sources:
        for line_no, line in enumerate(body.split("\n"), start=1):
            # The closure note names the removed ids on purpose.
            # Only the ledger's own closure roster may name a removed id.
            # A broad "contains this phrase" skip let stale prose hide behind
            # it, so the exception is the exact roster lines and nothing else.
            if CLOSURE_ROSTER.match(line.strip()):
                continue
            for raw in GAP_REF.findall(line):
                number = int(raw)
                if number in REMOVED:
                    problems.append(
                        f"{name}:{line_no}: cites #{number}, which was closed "
                        f"upstream and removed from the ledger"
                    )
                elif (
                    number not in live
                    and number not in PREEXISTING_DANGLING
                    and number >= 30
                ):
                    problems.append(
                        f"{name}:{line_no}: cites #{number}, which is not an "
                        f"entry in afterhours_gaps.md"
                    )
    return problems


# Where a closed gap can still be described as live. The ledger and the index
# were the only things checked, so docs, scripts and source kept saying that
# fixed defects were current -- which is the same debt in a place nobody looks.
# Removed CONTROL names -- an env var or flag that no longer exists is as stale
# as a removed gap id, and reads as current guidance.
REMOVED_CONTROLS = ("HANABI_STRESS_RESIZE_BACKEND",)
# Text that may legitimately name a removed id or control: the index's closure
# table, and explicitly historical evidence that points at the fixing commit.
HISTORICAL = re.compile(
    r"(?i)(^\|\s*#\d{2,4}\s*\|\s*`[0-9a-f]{7}`|"
    r"\b(was|were|used to|formerly|historical|at the time|before|"
    r"fixed (?:by|in)|closed by|landed (?:as|in)|no longer)\b)"
)
# Literal fixture text that merely LOOKS like a citation.
FIXTURE_LITERALS = (
    "is the follow-up",          # mock_client.h's "#42 is the follow-up ticket"
    "kid-local",                 # test_agentcloud_local.cpp
    'plus a "#42" line',         # the markdown fixture's own description
)

# Text a MECHANICAL id-repointing pass leaves behind. Repointing "#17" to prose
# produced "gap gap fixed upstream by 817d00e" in 15 places and, worse, kept
# claims whose subject was the closed gap -- the sentence still said
# text_input forces Secondary while the call below it passed
# with_transparent_bg. A stale claim that no longer names an id is invisible to
# the id checks, so it needs its own.
MALFORMED = (
    ("gap gap", "a doubled word from a mechanical id replacement"),
    ("gap fixed upstream by", "prose left in an id slot by a mechanical replacement"),
)
# These are FALSE of text_input at this pin (component.h:171-178 honours a
# caller's background, :216 an explicit font size). They remain TRUE of
# text_area, which is gap #262 -- so the check is scoped to lines naming
# text_input.
FALSE_TEXT_INPUT_CLAIMS = (
    "ignores with_custom_background",
    "forces its own Secondary",
    "forces its field background",
)
# A line that SAYS it is talking about a gap. Used only by the
# does-this-id-exist arm: a bare number is usually not a gap id (an HTTP close
# code, a review item), while a removed id is stale wherever it appears.
GAP_CONTEXT = re.compile(r"(?i)\b(afterhours[ _]gaps?|gap)\b")
REPO_WIDE = ("docs", "scripts", "src", "tests")
SKIP_DIRS = {"vendor", "output", "test-failures", ".git"}


def check_repo_wide(root: Path) -> list[str]:
    """No file outside the ledger may cite a removed or non-existent gap id."""
    problems = []
    ledger = root / "afterhours_gaps.md"
    live = {e.number for e in parse_entries(ledger.read_text())} if ledger.is_file() else set()
    index = root / "afterhours_gaps_index.md"
    if index.is_file():
        live |= {
            int(m.group(1))
            for m in re.finditer(r"(?m)^\|\s*(\d{2,4})\s*\|", index.read_text())
        }
    for top in REPO_WIDE:
        base = root / top
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in {
                ".md", ".h", ".cpp", ".mm", ".py", ".sh", ".e2e"
            }:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            # This file's own REMOVED roster and comments name those ids by
            # definition; it cannot be its own violation.
            if path.name == "check_gap_references.py":
                continue
            try:
                body = path.read_text()
            except (UnicodeDecodeError, OSError):
                continue
            rel = path.relative_to(root).as_posix()
            # Scan every line, NOT only lines that also say "gap". A stale
            # paragraph routinely puts "afterhours_gaps.md" on one line and
            # "#200" three lines later, and a same-line rule walks straight
            # past it -- which is exactly how the perf docs kept describing
            # closed gaps as live through two review rounds.
            file_lines = body.split("\n")
            for line_no, line in enumerate(file_lines, start=1):
                if any(lit in line for lit in FIXTURE_LITERALS):
                    continue
                if HISTORICAL.search(line):
                    continue
                for raw in GAP_REF.findall(line):
                    number = int(raw)
                    if number in REMOVED:
                        problems.append(
                            f"{rel}:{line_no}: cites #{raw}, closed upstream "
                            f"and removed from the ledger"
                        )
                    elif (
                        number not in live
                        and number not in PREEXISTING_DANGLING
                        and 30 <= number < 1000
                        # Only where the line SAYS it is a gap. A bare number
                        # is usually not one: http_client.cpp's "#4091" is a
                        # close code, ui-chrome-audit's are review item ids.
                        and GAP_CONTEXT.search(line)
                    ):
                        # A source file citing an id that is in NO ledger and
                        # NO index row. The removed-id arm alone missed these:
                        # #309 was cited by main_pane_system.h and has never
                        # existed anywhere, at origin or here.
                        problems.append(
                            f"{rel}:{line_no}: cites #{raw}, which is not an "
                            f"entry in afterhours_gaps.md"
                        )
                for phrase, why in MALFORMED:
                    if phrase in line:
                        problems.append(
                            f"{rel}:{line_no}: contains {phrase!r} — {why}"
                        )
                if "text_input" in line and "text_area" not in line:
                    for claim in FALSE_TEXT_INPUT_CLAIMS:
                        if claim in line:
                            problems.append(
                                f"{rel}:{line_no}: says text_input {claim!r}, "
                                f"which upstream 817d00e made false"
                            )
                for control in REMOVED_CONTROLS:
                    if control in line:
                        problems.append(
                            f"{rel}:{line_no}: names {control}, a control that "
                            f"no longer exists"
                        )
    return problems


# The §6 triage table, parsed by SECTION rather than by identifier syntax.
#
# Enumerating the id grammar is what went wrong: a regex of
# `\d{2,4}|AN-\d+` silently skipped 28 valid rows -- the letter-suffixed
# family ids (`27a`, `31c`), the range id `550–559` (en dash), and the
# single-digit rows -- so the checker "verified" a count that was 28 short and
# agreed with a summary that was wrong the same way. Two numbers derived from
# the same blind spot cannot catch each other.
#
# So: take every Markdown data row between the §6 heading and its terminating
# `---`, skip only the header and the separator, and treat the first cell as
# the identifier whatever shape it has.
SECTION_HEADING = "## 6. Full triage table"
SEPARATOR_ROW = re.compile(r"^\|[\s:|-]+\|$")
HEADER_FIRST_CELL = re.compile(r"^(id|gap|#|identifier)$", re.I)
DECLARED = re.compile(
    r"\| \*\*Rows in the triage table \(§6\)\*\* \| \*\*(\d+)\*\* rows, "
    r"\*\*(\d+)\*\* unique"
)


def triage_identifiers(text: str) -> list[str]:
    """Every §6 data row's first cell, in order."""
    start = text.find(SECTION_HEADING)
    if start == -1:
        return []
    end = text.find("\n---", start)
    section = text[start : end if end != -1 else len(text)]
    out = []
    for line in section.split("\n"):
        line = line.rstrip()
        if not line.startswith("|") or SEPARATOR_ROW.match(line):
            continue
        cells = line.split("|")
        if len(cells) < 3:
            continue  # malformed: not a data row
        first = cells[1].strip()
        if not first or HEADER_FIRST_CELL.match(first):
            continue
        out.append(first)
    return out


def check_index_table(root: Path) -> list[str]:
    """§6 has no duplicate identifiers, and its declared counts are true."""
    index = root / "afterhours_gaps_index.md"
    if not index.is_file():
        return []
    text = index.read_text()
    ids = triage_identifiers(text)
    problems = []
    seen, dupes = set(), []
    for identifier in ids:
        if identifier in seen:
            dupes.append(identifier)
        seen.add(identifier)
    for identifier in sorted(set(dupes)):
        problems.append(
            f"afterhours_gaps_index.md: §6 lists {identifier} more than once"
        )
    match = DECLARED.search(text)
    if match is None:
        problems.append(
            "afterhours_gaps_index.md: the §6 summary does not declare "
            "'**N** rows, **M** unique' and cannot be checked"
        )
    else:
        rows, uniq = int(match.group(1)), int(match.group(2))
        if rows != len(ids):
            problems.append(
                f"afterhours_gaps_index.md: §6 declares {rows} rows but the "
                f"table has {len(ids)}"
            )
        if uniq != len(seen):
            problems.append(
                f"afterhours_gaps_index.md: §6 declares {uniq} unique ids but "
                f"the table has {len(seen)}"
            )
    return problems


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


def selftest_triage_parser() -> int:
    """Every id shape the table actually uses, plus the failure modes.

    These exist because the first version of this parser enumerated the id
    grammar and silently skipped 28 valid rows -- suffixed ids, the en-dash
    range, and single digits -- then agreed with a summary that was wrong the
    same way.
    """
    table = "\n".join([
        "## 6. Full triage table",
        "",
        "| id | title | class | pain | size | status |",
        "|---|---|---|---|---|---|",
        "| 7 | a single digit | — | LOW | XS | live |",
        "| 374 | plain numeric | BLOCKING | HIGH | XS | live |",
        "| 27a | a letter-suffixed family id | — | MED | S | live |",
        "| 31c | a third letter suffix | — | MED | S | live |",
        "| AN-9 | an animation id | MISSING | MED | L | live |",
        "| 550\u2013559 | an en-dash RANGE id | — | LOW | M | live |",
        "| malformed-no-closing-pipe",
        "",
        "---",
    ])
    got = triage_identifiers(table)
    want = ["7", "374", "27a", "31c", "AN-9", "550\u2013559"]
    failures = []
    if got != want:
        failures.append(f"parsed {got}, expected {want}")

    # A duplicate letter-suffixed id must be caught, not just a numeric one.
    dupe = table.replace(
        "| 31c | a third letter suffix | — | MED | S | live |",
        "| 31c | a third letter suffix | — | MED | S | live |\n"
        "| 27a | the same suffixed id again | — | MED | S | live |",
    )
    ids = triage_identifiers(dupe)
    if len(ids) - len(set(ids)) != 1:
        failures.append("a duplicate suffixed id was not visible to the parser")

    # Rows outside the section must not be counted.
    outside = table + "\n\n## 7. Something else\n\n| 999 | not §6 | — | — | — | — |\n"
    if triage_identifiers(outside) != want:
        failures.append("rows after the section terminator were counted")

    if failures:
        print("check_gap_references triage-parser selftest: FAIL")
        for f in failures:
            print(f"  {f}")
        return 1
    print("check_gap_references triage-parser selftest: 6 id shapes, a "
          "duplicate, and a section boundary all handled")
    return 0


def selftest_repo_wide() -> int:
    """The repo-wide arm's own rules, since its bugs are silent by nature."""
    cases = [
        # (line, expect_problem, why)
        ("`HANABI_STRESS_RESIZE_BACKEND=1` reproduces it in one run.", True,
         "a removed control named as current guidance"),
        ("gap #200 is a 4.8 MB-per-1000-frame leak", True,
         "a removed id, with gap context"),
        ("the leak lives on. See #200 for the numbers.", True,
         "a removed id with NO gap word on the line -- the same-line rule "
         "missed exactly this and let the perf docs rot"),
        ("fixed upstream by 1ad3360, which used to be #200", False,
         "explicitly historical, points at the fixing commit"),
        ("| #200 | `1ad3360` | render-pipeline leak per resize |", False,
         "the closure table row"),
        ('"#42 is the follow-up ticket."', False,
         "literal fixture text in mock_client.h"),
        ("#374 is the frame-boundary constraint", False,
         "a LIVE gap id"),
    ]
    failed = []
    for line, expect, why in cases:
        hit = False
        if not any(lit in line for lit in FIXTURE_LITERALS):
            if not HISTORICAL.search(line):
                hit = any(int(r) in REMOVED for r in GAP_REF.findall(line))
                hit = hit or any(c in line for c in REMOVED_CONTROLS)
        if hit != expect:
            failed.append(f"{why}: expected {expect}, got {hit} for {line!r}")
    if failed:
        print("check_gap_references selftest: FAIL")
        for f in failed:
            print(f"  {f}")
        return 1
    print(f"check_gap_references selftest: {len(cases)} repo-wide cases passed")
    return 0


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest_triage_parser() or selftest_repo_wide()
    parser = argparse.ArgumentParser()
    parser.add_argument("--document", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    root = (args.root or Path(__file__).resolve().parent.parent).resolve()
    document = (args.document or root / "afterhours_gaps.md").resolve()
    text = document.read_text()
    problems = (
        check_document(text, root)
        + check_cross_references(text, root)
        + check_symbol_claims(text, root)
        + check_repo_wide(root)
        + check_index_table(root)
    )
    if problems:
        print("check_gap_references: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    count = len(parse_entries(text))
    print(f"check_gap_references: {count} entries; every workaround path and "
          f"every cited gap id resolves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
