#!/usr/bin/env python3
"""scripts/check_autorelease.py — the four lines nobody will miss.

WHAT THIS GUARDS.  Metal hands back autoreleased Objective-C objects:
``sg_begin_pass`` alone produces a command buffer, a render-pass descriptor and
three attachment descriptors, none of them owned by the caller.  A Cocoa run
loop drains its pool every iteration; a bare render loop has nothing draining
it, so every frame's objects stay live for the life of the process — measured
at ~2.5 KB a frame, ~9 MB a minute, which is the whole of the reported "it gets
slower and slower every second until it freezes".

The fix is ``hanabi::AutoreleaseFrame`` (src/util/autorelease.h), one scoped
line at the top of each frame loop.  It has no callers, returns nothing, and
reads as dead code.  Someone tidying ``main.cpp`` will delete it, and the app
will look completely fine for the first thirty seconds.

The soak gate (scripts/soak_gate.sh) catches the deletion in about a second and
a half of runtime, and is the real guard.  This is the cheap one that runs on
the source, names the exact line, and — unlike the soak gate — can also see the
loops the soak gate never executes (the scripted-UI loop, the frame-timing
loop), because it does not have to run them.

WHAT IT CHECKS.  Every ``graphics::begin_frame()`` in src/ must be inside a
scope — its own or an enclosing one, up to the enclosing function — that
declares a ``hanabi::AutoreleaseFrame``.  Plus: the pool type itself must still
push and pop a real Objective-C pool on Apple, so that stubbing the class out
is caught as loudly as deleting its uses.

Exit 0 = clean.  Exit 1 = at least one unpooled frame loop.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUTORELEASE_H = ROOT / "src" / "util" / "autorelease.h"

BEGIN_FRAME = re.compile(r"\bgraphics::begin_frame\s*\(")
POOL_DECL = re.compile(r"\bAutoreleaseFrame\b\s+\w+\s*(?:;|\{|=)")


def strip_noise(line: str) -> str:
    """Remove // comments and string/char literal bodies.

    Only the braces matter here, and a brace inside a string or a comment
    would push a scope that never closes — which silently makes every later
    check meaningless.  Block comments are handled by the caller, which tracks
    the /* */ state across lines.
    """
    out = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            break
        if c in "\"'":
            quote = c
            i += 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def check_file(path: Path):
    """Return a list of (lineno, text) for begin_frame calls with no pool."""
    failures = []
    # One bool per open brace: does this scope declare an AutoreleaseFrame?
    scopes = [False]
    in_block_comment = False
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw
        if in_block_comment:
            end = line.find("*/")
            if end < 0:
                continue
            line = line[end + 2 :]
            in_block_comment = False
        while True:
            start = line.find("/*")
            if start < 0:
                break
            end = line.find("*/", start + 2)
            if end < 0:
                line = line[:start]
                in_block_comment = True
                break
            line = line[:start] + " " + line[end + 2 :]

        code = strip_noise(line)

        if POOL_DECL.search(code):
            scopes[-1] = True

        if BEGIN_FRAME.search(code) and not any(scopes):
            failures.append((lineno, raw.strip()))

        for ch in code:
            if ch == "{":
                scopes.append(False)
            elif ch == "}":
                if len(scopes) > 1:
                    scopes.pop()
                else:
                    # Unbalanced input: stop trusting the scope stack rather
                    # than reporting confident nonsense about later lines.
                    return failures, False
    return failures, True


def check_pool_type() -> list:
    """The pool class must still do the thing its name claims."""
    problems = []
    if not AUTORELEASE_H.exists():
        return [f"{AUTORELEASE_H.relative_to(ROOT)} is gone"]
    text = AUTORELEASE_H.read_text()
    for symbol in ("objc_autoreleasePoolPush", "objc_autoreleasePoolPop"):
        if symbol not in text:
            problems.append(
                f"{AUTORELEASE_H.relative_to(ROOT)} no longer calls {symbol} — "
                "the pool has been stubbed out, so every frame loop that opens "
                "one is now opening nothing"
            )
    if "#if defined(__APPLE__)" not in text:
        problems.append(
            f"{AUTORELEASE_H.relative_to(ROOT)}: the Apple branch is no longer "
            "guarded by a bare `#if defined(__APPLE__)`. If a second condition "
            "was added, a build can now silently compile the no-op pool on a Mac"
        )
    return problems


def main() -> int:
    sources = sorted((ROOT / "src").rglob("*.cpp")) + sorted(
        (ROOT / "src").rglob("*.h")
    )
    all_failures = []
    unparsed = []
    for path in sources:
        text = path.read_text()
        if "graphics::begin_frame" not in text:
            continue
        failures, ok = check_file(path)
        if not ok:
            unparsed.append(path)
        for lineno, snippet in failures:
            all_failures.append((path, lineno, snippet))

    problems = check_pool_type()

    if not all_failures and not problems and not unparsed:
        print("check_autorelease: every graphics::begin_frame() is inside an "
              "AutoreleaseFrame scope")
        return 0

    print("check_autorelease: FAIL")
    for path, lineno, snippet in all_failures:
        rel = path.relative_to(ROOT)
        print(f"  {rel}:{lineno}: graphics::begin_frame() with no autorelease "
              f"pool in scope")
        print(f"      {snippet}")
    if all_failures:
        print()
        print("  A frame loop that calls into Metal without a pool leaks the")
        print("  render pass's autoreleased objects — about 2.5 KB a frame,")
        print("  ~9 MB a minute at 60fps, never returned. Add, as the first")
        print("  line inside the loop body:")
        print()
        print("      const hanabi::AutoreleaseFrame framePool;")
        print()
        print("  and #include \"util/autorelease.h\" if it is not already there.")
        print("  See src/util/autorelease.h for the measurement, and")
        print("  docs/perf/GATES.md for how to watch this fail on purpose.")
    for problem in problems:
        print(f"  {problem}")
    for path in unparsed:
        print(f"  {path.relative_to(ROOT)}: braces did not balance; this check "
              "could not read the file, so treat its result as unknown")
    return 1


if __name__ == "__main__":
    sys.exit(main())
