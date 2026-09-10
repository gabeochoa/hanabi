#!/usr/bin/env python3
"""scripts/check_watchdogs.py — the background sleep that holds the pipe open.

WHAT THIS GUARDS.  A gate that bounds its child's run time writes, naturally:

    ( sleep "$RUN_TIMEOUT"; kill -9 "$APP_PID"; kill_own_runs ) &
    WATCH_PID=$!
    wait "$APP_PID"
    kill "$WATCH_PID"

Two things are wrong with it and neither is visible on a terminal.
``kill "$WATCH_PID"`` kills the subshell, not the ``sleep`` it is blocked in,
so the sleep is reparented and runs to completion; and that orphan inherited
the script's stdout, so any reader of that stdout — ``make``, ``tee``,
``| cat``, a CI log capture — blocks until the full timeout elapses.

Measured on 2026-08-25, clean tree, ``>file`` against ``2>&1 | cat``:
``soak_gate.sh`` 4 s -> 120 s (its RUN_TIMEOUT is 120), ``measure_launch.sh``
0 s -> 16 s (its RUN_TIMEOUT is 15).  Both run inside ``make test``, so a
piped suite paid 135 seconds of sleeping for nothing, and every wall-clock
figure in docs/perf/GATES.md was taken on a tty where the bug does not show.

WHAT IT CHECKS.  Every backgrounded compound command in scripts/ that can
sleep must send its own stdout somewhere that is not the caller's.  That is
the property that causes the stall, and it is the one a reader cannot see.
Use ``scripts/watchdog.sh``, which does that and also polls so there is no
orphan in the first place.

WHY THE STDOUT RULE AND NOT "DO NOT ORPHAN A SLEEP".  Whether a background job
leaves an orphan depends on how it is killed, which is several lines away and
not decidable by reading one line.  Whether it holds the caller's stdout is
decidable from the line itself, it is the half that costs the time, and a job
that redirects is harmless even when it IS orphaned.  So the check gates the
half it can see and names the other half in the message.

WHAT ELSE IT CHECKS.  No script calls ``timeout`` or ``gtimeout``.  macOS
ships neither, and the ``/usr/local/bin/timeout`` a homebrew install once
linked on these Macs is a dangling symlink, so the call exits 127 with an
empty reading, which a gate that discards stderr reports as a product failure
(nine FAIL rows on a clean main, every one reading "got /0 calls").
``watchdog_run`` in ``scripts/watchdog.sh`` is the bound to use.

Exit 0 = clean.  Exit 1 = at least one unredirected sleeping background job
or one timeout(1) call.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts"

# `( ... ) &` or `{ ...; } &`, all on one line, with the trailing `&` being a
# background operator rather than `&&`. Anything spanning lines is reported as
# unreadable rather than guessed at.
BACKGROUNDED = re.compile(r"^\s*(?P<body>[({].*[)}])\s*(?P<tail>.*)$")
SLEEPS = re.compile(r"\bsleep\s")
# A redirect of stdout (or of everything) applied to the job. `>/dev/null`,
# `> /dev/null`, `&>/dev/null`, `>&-`, or a redirect into a file.
REDIRECTS_STDOUT = re.compile(r"(^|\s)(&>|\d?>)")
# `timeout` or `gtimeout` in command position: not `--timeout`, `$timeout`,
# `./timeout`, and not a word inside a quoted string.
TIMEOUT_BINARY = re.compile(r"(?<![\w\-./])g?timeout\s")


def strip_comment(line: str) -> str:
    """Drop a trailing # comment, respecting quotes.

    A `#` inside a quoted string is not a comment, and treating one as a
    comment would silently truncate the line this check is reading.
    """
    out = []
    quote = None
    i = 0
    while i < len(line):
        c = line[i]
        if quote is None and c == "\\":
            out.append(c)
            if i + 1 < len(line):
                out.append(line[i + 1])
            i += 2
            continue
        if quote is None and c in "\"'":
            quote = c
        elif quote is not None and c == quote:
            quote = None
        elif quote is None and c == "#" and (i == 0 or line[i - 1].isspace()):
            break
        out.append(c)
        i += 1
    return "".join(out)


def offenders(path: Path):
    """(lineno, text) for each backgrounded sleeping job with no own stdout."""
    found = []
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        code = strip_comment(raw)
        if not code.strip().endswith("&") or code.strip().endswith("&&"):
            continue
        job = code.strip()[:-1].strip()
        if not SLEEPS.search(job):
            continue
        # The redirect must be OUTSIDE the compound command: a redirect inside
        # it applies to one command in the job, not to the job, and the
        # descriptor is inherited before that inner command ever runs.
        m = BACKGROUNDED.match(job)
        tail = m.group("tail") if m else job
        if REDIRECTS_STDOUT.search(tail):
            continue
        found.append((lineno, raw.strip()))
    return found


def inside_quotes(text: str) -> bool:
    stack = []
    i = 0
    while i < len(text):
        c = text[i]
        top = stack[-1] if stack else None
        if top == "'":
            if c == "'":
                stack.pop()
        elif c == "\\":
            i += 2
            continue
        elif text.startswith("$(", i):
            stack.append("(")
            i += 2
            continue
        elif top == '"':
            if c == '"':
                stack.pop()
        elif c in "\"'":
            stack.append(c)
        elif c == ")" and top == "(":
            stack.pop()
        i += 1
    return bool(stack) and stack[-1] in "\"'"


def timeout_offenders(path: Path):
    """(lineno, text) for each line that invokes timeout(1) or gtimeout."""
    found = []
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        code = strip_comment(raw)
        for m in TIMEOUT_BINARY.finditer(code):
            if not inside_quotes(code[: m.start()]):
                found.append((lineno, raw.strip()))
                break
    return found


def main() -> int:
    if not SCRIPTS.is_dir():
        print("check_watchdogs: scripts/ is gone; nothing could be checked, so "
              "treat this result as unknown rather than clean")
        return 1

    scanned = 0
    all_found = []
    timeouts = []
    for path in sorted(SCRIPTS.glob("*.sh")):
        scanned += 1
        for lineno, snippet in offenders(path):
            all_found.append((path, lineno, snippet))
        for lineno, snippet in timeout_offenders(path):
            timeouts.append((path, lineno, snippet))

    if scanned == 0:
        print("check_watchdogs: no shell scripts found under scripts/. This "
              "check reads *.sh and there were none, which is a broken check "
              "rather than a clean tree")
        return 1

    if not all_found and not timeouts:
        print(f"check_watchdogs: {scanned} scripts, no background sleep holds "
              "the caller's stdout, no timeout(1) call")
        return 0

    print("check_watchdogs: FAIL")
    if timeouts:
        for path, lineno, snippet in timeouts:
            print(f"  {path.relative_to(ROOT)}:{lineno}: timeout(1) invoked")
            print(f"      {snippet}")
        print()
        print("  macOS ships no timeout(1). On these Macs /usr/local/bin/timeout")
        print("  is a dangling homebrew symlink, so this line exits 127 with an")
        print("  empty reading, and a gate that discards stderr reports that as")
        print("  a product failure. Bound the run with the shared helper:")
        print()
        print('      . "$ROOT/scripts/watchdog.sh"')
        print('      watchdog_run "$RUN_TIMEOUT" "$EXE" args... >"$LOG" 2>&1')
        print()
    if not all_found:
        return 1
    for path, lineno, snippet in all_found:
        print(f"  {path.relative_to(ROOT)}:{lineno}: a backgrounded job that "
              "sleeps, with the caller's stdout")
        print(f"      {snippet}")
    print()
    print("  A pipe stays open until every writer closes it. This job inherited")
    print("  the script's stdout, and if it outlives the script — which a")
    print("  `sleep` in a subshell does, because killing the subshell does not")
    print("  kill the sleep — then `make`, `tee`, `| cat` and every CI log")
    print("  capture block for the whole timeout after the gate has finished.")
    print("  It costs nothing on a terminal, which is why it survived.")
    print()
    print("  Use the shared helper, which redirects AND polls so there is no")
    print("  orphan to redirect:")
    print()
    print('      . "$ROOT/scripts/watchdog.sh"')
    print('      "$EXE" args... &')
    print('      APP_PID=$!')
    print('      watchdog_start "$APP_PID" "$RUN_TIMEOUT" kill_own_runs')
    print('      wait "$APP_PID"; rc=$?')
    print('      watchdog_stop')
    print()
    print("  See scripts/watchdog.sh for the measurement.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
