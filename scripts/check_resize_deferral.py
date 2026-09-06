#!/usr/bin/env python3
"""scripts/check_resize_deferral.py — no resize outside the frame boundary.

afterhours' window_manager::set_window_size() destroys and recreates the
headless render target inline. Called from inside a System -- which runs
between graphics::begin_frame() and graphics::end_frame() -- that tears down
the attachments of a pass that is still open, with draw commands still
recorded against them, and sokol aborts the process:

    VALIDATE_APIP_ATTACHMENTS_ALIVE: sg_apply_pipeline: at least one pass
    attachment view or base image object is no longer alive

src/util/gfx_resize.h splits the operation instead: the resolution moves
immediately and the render target moves at the next frame boundary, before
begin_frame opens the pass that will draw into it.

That only holds while every caller goes through it. So:

  1. Only src/util/gfx_resize.h may call window_manager::set_window_size.
  1b. Nothing outside it may call graphics::begin_frame directly. The
     deferral is DRAINED by hanabi::gfx::begin_frame, so a frame loop that
     opens its frame any other way arms resizes that never reach the backend:
     the scenario looks busy and the target never moves. Both soak loops in
     main.cpp did exactly that when the deferral first landed, and no test
     noticed, because the count the run printed was the count REQUESTED.
     scripts/stress_resize_gate.sh catches it at runtime; this catches it in
     review.
  2. Every .e2e script must use the deferred path. Hanabi consumes `resize`
     ahead of afterhours' builtin, so a script may keep saying `resize` -- but
     the interception is what makes that safe, and
     tests/ui/a_resize_moves_the_target_at_a_frame_boundary.e2e is the planted
     control that proves the interception still happens. This checks that
     control still exists and still asserts a non-zero applied count, because
     a suite that quietly dropped it would go green on a broken deferral.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OWNER = "src/util/gfx_resize.h"
# The WINDOWED path is a different resize and is exempt on purpose: it resizes
# a real NSWindow and never touches the offscreen render target, so it cannot
# reach the mid-pass teardown this file exists to prevent (afterhours_gaps.md
# #374 says so, and the leak measurement that started it was headless-only).
# Named exactly, so a NEW call still fails.
WINDOWED_PATH_EXEMPT = {
    ("src/sokol_impl.mm", "metal_set_window_size"),   # the definition
    ("src/main.cpp", "metal_set_window_size"),        # extern decl + GPU watch
}
CONTROL = "tests/ui/a_resize_moves_the_target_at_a_frame_boundary.e2e"
# Both routes to the backend: the library's, and the app's own Objective-C
# entry point that resizes the real NSWindow. A deferral that owns one and not
# the other is not an invariant.
CALL = re.compile(r"(?:window_manager::set_window_size|metal_set_window_size)"
                  r"\s*\(")
OPEN_FRAME = re.compile(r"graphics::begin_frame\s*\(")
APPLIED = re.compile(r"^expect_resizes_applied\s+([1-9]\d*)\s*$", re.M)


def check() -> list[str]:
    problems: list[str] = []

    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".h", ".cpp", ".mm"} or not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel == OWNER:
            continue
        text = path.read_text()
        for match in CALL.finditer(text):
            if (rel, match.group(0).split("(")[0].strip()) in WINDOWED_PATH_EXEMPT:
                continue
            # A mention inside a comment is documentation, not a call.
            line_start = text.rfind("\n", 0, match.start()) + 1
            line = text[line_start:text.find("\n", match.start())]
            if line.lstrip().startswith(("//", "*", "/*")):
                continue
            number = text.count("\n", 0, match.start()) + 1
            problems.append(
                f"{rel}:{number}: calls window_manager::set_window_size "
                f"directly — route it through hanabi::gfx::request_resize so "
                f"the render target moves at a frame boundary ({OWNER})"
            )

    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".h", ".cpp", ".mm"} or not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel == OWNER:
            continue
        text = path.read_text()
        for match in OPEN_FRAME.finditer(text):
            line_start = text.rfind("\n", 0, match.start()) + 1
            end = text.find("\n", match.start())
            line = text[line_start:end if end != -1 else len(text)]
            if line.lstrip().startswith(("//", "*", "/*")):
                continue
            number = text.count("\n", 0, match.start()) + 1
            problems.append(
                f"{rel}:{number}: opens a frame with graphics::begin_frame — "
                f"use hanabi::gfx::begin_frame so a requested resize is "
                f"actually applied ({OWNER})"
            )

    control = ROOT / CONTROL
    if not control.is_file():
        problems.append(
            f"{CONTROL}: the resize-deferral control is missing — without it "
            f"a deferral that stopped deferring still reads green"
        )
    else:
        body = control.read_text()
        if not APPLIED.search(body):
            problems.append(
                f"{CONTROL}: no `expect_resizes_applied N` with N >= 1 — the "
                f"control no longer proves a resize reached a frame boundary"
            )

    return problems


def main() -> int:
    if "--selftest" in sys.argv:
        cases = [
            ("direct call", "window_manager::set_window_size(w, h);", False),
            ("commented", "// window_manager::set_window_size(w, h);", True),
            ("routed", "hanabi::gfx::request_resize(w, h);", True),
        ]
        failed = []
        for name, body, should_pass in cases:
            hit = False
            for match in CALL.finditer(body):
                line_start = body.rfind("\n", 0, match.start()) + 1
                end = body.find("\n", match.start())
                line = body[line_start:end if end != -1 else len(body)]
                if not line.lstrip().startswith(("//", "*", "/*")):
                    hit = True
            if (not hit) != should_pass:
                failed.append(name)
        if failed:
            print("check_resize_deferral selftest: FAIL: " + ", ".join(failed))
            return 1
        print(f"check_resize_deferral selftest: {len(cases)} cases passed")
        return 0

    problems = check()
    if problems:
        print("check_resize_deferral: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("check_resize_deferral: set_window_size is owned by "
          f"{OWNER}, and the frame-boundary control is live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
