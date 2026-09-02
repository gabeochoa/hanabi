#!/usr/bin/env python3
"""scripts/check_sidebar_scan.py — the sidebar's catalog walks, counted.

WHAT THIS GUARDS, AND WHY A COUNTER GATE COULD NOT.  `make sidebar-scan-gate`
reads counters that `ecs::model::SidebarBuckets` publishes about ITSELF:
rebuilds, reuses, sessions visited.  Every one of them is an honest reading of
the collection — and every one of them stays green if somebody puts a raw loop
over `app.sessions` back into `render_folder`, because a loop that never enters
`SidebarBuckets` is a loop `SidebarBuckets` cannot report.  The gate would show
`rebuilds 2, reuse 722, ratio 1.000` over a panel walking the catalog (F+2)
times a frame.  That was the first finding of the review of the change that
added the gate.

So the property is checked where it is decidable: in the source.  Two rules.

  1. The functions that run PER FOLDER or PER ROW may not name the catalog at
     all.  `render_folder` collecting its own members is the exact regression;
     it is also the only way the (F+2) shape can come back.

  2. Everywhere else in the file, the walks are a fixed, listed set.  Adding
     one is allowed — `view_counts` is a deliberate per-frame walk and says so
     where it is defined — but it has to be a decision somebody wrote down
     here, not a line that slid in.

Exit 0 = clean.  Exit 1 = a new walk, or a walk somewhere it must never be.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = "src/ecs/sidebar_system.h"

# Rule 1: these may not touch the catalog at all.
FORBIDDEN_IN = ("render_folder", "render_group", "render_chat_row")

# Rule 2: the walks that are allowed to exist, by the function that holds them.
# Three one-shot request handlers in `for_each_with` (they run only when a
# toggle request is pending and they `break` on the match), and `view_counts`,
# the deliberate per-frame pass for the two smart-view badges.
BASELINE = {
    "for_each_with": 3,
    "view_counts": 1,
}

# A MENTION is any reference to the catalog; a WALK is a loop over it. Handing
# the vector to a collaborator (`buckets_.rebuild(..., app->sessions, ...)`,
# `parentIndex_.update(..., app.sessions, ...)`) is a mention and not a walk,
# and the two rules below want different ones: the per-folder functions may not
# even MENTION it, and everywhere else it is the WALKS that are counted.
CATALOG = re.compile(r"\bapp\s*(?:\.|->)\s*sessions\b")
WALK = re.compile(r"\bfor\s*\([^;)]*:\s*app\s*(?:\.|->)\s*sessions\s*\)")
# `<indent 4><stuff> name(` — the member-function shape this file uses.
SIGNATURE = re.compile(r"^    (?:[A-Za-z_][\w:<>,*& ]*?[ *&])?([A-Za-z_]\w*)\(",
                       re.M)


def function_spans(text: str) -> list[tuple[str, int, int]]:
    """(name, start, end) for each member function, by brace matching."""
    out = []
    for m in SIGNATURE.finditer(text):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "return", "catch"):
            continue
        brace = text.find("{", m.end())
        if brace < 0:
            continue
        # A declaration with no body, or a signature that ran past its own
        # statement, is not a function span.
        if ";" in text[m.end():brace]:
            continue
        depth = 0
        i = brace
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append((name, m.start(), i))
    return out


def main() -> int:
    path = ROOT / TARGET
    text = path.read_text()
    spans = function_spans(text)
    line_of = lambda pos: text.count("\n", 0, pos) + 1  # noqa: E731

    def holder_of(pos: int):
        found = None
        for name, start, end in spans:
            if start <= pos <= end:
                found = name  # innermost wins: later spans are nested
        return found

    failures = []
    for m in CATALOG.finditer(text):
        holder = holder_of(m.start())
        if holder in FORBIDDEN_IN:
            failures.append(
                f"{TARGET}:{line_of(m.start())}: {holder}() names the session "
                f"catalog. Per-folder collection is what SidebarBuckets "
                f"replaced, and the counter gate cannot see it come back.")

    seen: dict[str, int] = {}
    for m in WALK.finditer(text):
        holder = holder_of(m.start())
        if holder is None or holder in FORBIDDEN_IN:
            continue
        seen[holder] = seen.get(holder, 0) + 1

    for holder, count in sorted(seen.items()):
        allowed = BASELINE.get(holder)
        if allowed is None:
            failures.append(
                f"{TARGET}: {holder}() walks the session catalog and is not in "
                f"this script's BASELINE. If the walk is deliberate, add it "
                f"here with the reason; if it is not, route it through "
                f"ecs::model::SidebarBuckets.")
        elif count > allowed:
            failures.append(
                f"{TARGET}: {holder}() walks the session catalog {count} "
                f"times, baseline {allowed}.")

    for holder, allowed in sorted(BASELINE.items()):
        if seen.get(holder, 0) < allowed:
            failures.append(
                f"{TARGET}: {holder}() walks the catalog {seen.get(holder, 0)} "
                f"times, fewer than the baseline {allowed}. If a walk was "
                f"removed, lower the baseline — a stale one hides the next "
                f"addition.")

    if failures:
        for f in failures:
            sys.stderr.write(f"check_sidebar_scan: {f}\n")
        return 1

    total = sum(seen.values())
    print(f"check_sidebar_scan: {total} catalog walks, all listed; "
          f"{', '.join(FORBIDDEN_IN)} touch it none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
