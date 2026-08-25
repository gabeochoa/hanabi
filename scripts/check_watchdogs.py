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

Exit 0 = clean.  Exit 1 = at least one unredirected sleeping background job.
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


def main() -> int:
    if not SCRIPTS.is_dir():
        print("check_watchdogs: scripts/ is gone; nothing could be checked, so "
              "treat this result as unknown rather than clean")
        return 1

    scanned = 0
    all_found = []
    for path in sorted(SCRIPTS.glob("*.sh")):
        scanned += 1
        for lineno, snippet in offenders(path):
            all_found.append((path, lineno, snippet))

    if scanned == 0:
        print("check_watchdogs: no shell scripts found under scripts/. This "
              "check reads *.sh and there were none, which is a broken check "
              "rather than a clean tree")
        return 1

    if not all_found:
        print(f"check_watchdogs: {scanned} scripts, no background sleep holds "
              "the caller's stdout")
        return 0

    print("check_watchdogs: FAIL")
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
