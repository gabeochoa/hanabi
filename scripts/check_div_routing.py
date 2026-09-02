#!/usr/bin/env python3
"""scripts/check_div_routing.py — the one line that keeps every widget cheap."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMPORTS = "src/ecs/ui_imports.h"
WRAPPER = "src/ui/div.h"

BOUND = re.compile(r"^\s*using\s+hanabi::ui::div\s*;", re.M)
LIBRARY_BOUND = re.compile(r"^\s*using\s+afterhours::ui::imm::div\s*;", re.M)
DIRECT_CALL = re.compile(r"afterhours::ui::imm::div\s*\(")


def why() -> None:
    print()
    print("  afterhours' div() takes its ComponentConfig BY VALUE, and the")
    print("  fluent builder returns ComponentConfig& -- so an inline")
    print("  `ComponentConfig{}.with_label(...)` chain is an LVALUE and the")
    print("  parameter is COPY-constructed. Every std::string the config")
    print("  carries past libc++'s 22-character small-string buffer is one")
    print("  more malloc, per widget, per frame. hanabi::ui::div moves")
    print("  instead. Measured on the frozen base binary: home20 829 -> 740,")
    print("  home2000 1181 -> 1034, thread480 2707 -> 2599 allocations per")
    print("  frame (scripts/alloc_gate.sh, docs/perf/ALLOCATIONS.md).")
    print()
    print("  The binding is one `using` in %s and it reads as a" % IMPORTS)
    print("  redundant alias. Deleting it costs the app those allocations")
    print("  back, changes not one pixel, and no other check sees it:")
    print("  tests/unit/test_div_move.cpp proves the WRAPPER still moves,")
    print("  and the alloc gate's 20% headroom is wider than the loss.")


def main() -> int:
    imports = (ROOT / IMPORTS).read_text()
    problems: list[str] = []

    if not BOUND.search(imports):
        problems.append("%s: `using hanabi::ui::div;` is gone" % IMPORTS)
    if LIBRARY_BOUND.search(imports):
        problems.append("%s: binds div straight to afterhours" % IMPORTS)

    for path in sorted((ROOT / "src").rglob("*.h")) + sorted(
        (ROOT / "src").rglob("*.cpp")
    ):
        rel = path.relative_to(ROOT).as_posix()
        if rel == WRAPPER:
            continue
        for m in DIRECT_CALL.finditer(path.read_text()):
            line = path.read_text()[: m.start()].count("\n") + 1
            problems.append(
                "%s:%d: calls afterhours::ui::imm::div directly, "
                "bypassing the wrapper" % (rel, line)
            )

    if not problems:
        print("check_div_routing: ok (div routed through hanabi::ui::div)")
        return 0

    print("check_div_routing: FAIL")
    for p in problems:
        print("  %s" % p)
    why()
    return 1


if __name__ == "__main__":
    sys.exit(main())
