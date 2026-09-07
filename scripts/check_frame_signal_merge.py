#!/usr/bin/env python3
"""scripts/check_frame_signal_merge.py — the frame loop merges signals wholesale.

hanabi::FrameSignals is the union of everything that can justify a frame. The
windowed loop in main.cpp aggregates the app's signals into the frame's, and
it used to do that field by field. That copy is a silent list: a field added
to the struct and set by the collector is simply not carried, the compiler is
happy, and the feature that depends on it is dead only in the WINDOWED build.

That is exactly what happened to tooltip_dwell -- collected correctly, dropped
in the copy, so the idle reveal the headless harness measured at 0.5s took
seconds in the real app. Both the unit tests and the scripted UI suite ran
against continuously pumped loops and could not see it.

So the copy is gone: FrameSignals::merge_from() ORs every field and carries a
static_assert on sizeof so a new field breaks the build until it is merged.
This check keeps the loop on that path -- a hand-rolled field-by-field copy in
main.cpp would compile and would silently re-open the same hole.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOOP = ROOT / "src" / "main.cpp"
STRUCT = ROOT / "src" / "frame_activity.h"

FIELD_COPY = re.compile(
    r"^\s*frameSignals\.(\w+)\s*=\s*(?:frameSignals\.\1\s*\|\|\s*)?appSignals\.",
    re.M,
)


def check() -> list[str]:
    problems: list[str] = []
    loop = LOOP.read_text()
    struct = STRUCT.read_text()

    if "merge_from" not in struct:
        problems.append("src/frame_activity.h: FrameSignals has no merge_from")
    if "static_assert(sizeof(FrameSignals)" not in struct:
        problems.append(
            "src/frame_activity.h: merge_from has no sizeof static_assert, so "
            "a new field can be added without being merged")

    if "merge_from(hanabi::collect_app_frame_signals" not in loop:
        problems.append(
            "src/main.cpp: the frame loop does not merge the app signals "
            "wholesale via merge_from")

    stolen = sorted({m.group(1) for m in FIELD_COPY.finditer(loop)})
    if stolen:
        problems.append(
            "src/main.cpp: field-by-field signal copy is back for "
            + ", ".join(stolen)
            + " -- use merge_from so a new signal cannot be dropped")

    body = struct[struct.index("struct FrameSignals"):]
    body = body[: body.index("\n};")]
    declared = len(re.findall(r"^\s{4}bool \w+ = false;", body, re.M))
    merged = len(re.findall(r"^\s{8}\w+ = \w+ \|\| o\.\w+;", body, re.M))
    if declared and merged != declared:
        problems.append(
            f"src/frame_activity.h: {declared} signal fields declared but "
            f"{merged} merged in merge_from")
    return problems


def main() -> int:
    if "--selftest" in sys.argv:
        cases = [
            ("clean", "frameSignals.merge_from(hanabi::collect_app_frame_signals(app));", True),
            ("plain copy", "frameSignals.caret = appSignals.caret;", False),
            ("or copy", "frameSignals.timer = frameSignals.timer || appSignals.timer;", False),
            ("comment", "// frameSignals.caret = appSignals.caret;", True),
        ]
        failed = []
        for name, body, should_pass in cases:
            hit = False
            for match in FIELD_COPY.finditer(body):
                line_start = body.rfind("\n", 0, match.start()) + 1
                if not body[line_start:].lstrip().startswith(("//", "*", "/*")):
                    hit = True
            if (not hit) != should_pass:
                failed.append(name)
        if failed:
            print("check_frame_signal_merge selftest: FAIL: " + ", ".join(failed))
            return 1
        print(f"check_frame_signal_merge selftest: {len(cases)} cases passed")
        return 0

    problems = check()
    if problems:
        print("check_frame_signal_merge: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    text = STRUCT.read_text()
    body = text[text.index("struct FrameSignals"):]
    body = body[: body.index("\n};")]
    declared = len(re.findall(r"^\s{4}bool \w+ = false;", body, re.M))
    print(f"check_frame_signal_merge: ok ({declared} signals merged wholesale)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
