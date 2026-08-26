#!/usr/bin/env python3
"""Every environment variable build_seed() reads must be in kFixtureEnv.

WHY THIS EXISTS. src/api/mock_client.h caches the whole generated catalog and
keys the cache on the environment, and its own comment says why in the plainest
possible terms:

    Anything added to build_seed() that reads the environment MUST be added
    here too. That coupling is the price of the cache; the alternative was
    rebuilding a 2000-row catalog on every get_session().

The comment is there because the bug already happened once: the cache was keyed
on HANABI_STRESS_SESSIONS alone, the e2e runner loads a whole DIRECTORY of
scripts into ONE process and applies each script's own `# env:` line, so
whichever script ran first froze the fixture for every script after it. Two
scripts opened threads that did not exist and timed out waiting for text that
was never going to appear.

WHAT MAKES IT WORTH A CHECK RATHER THAN A COMMENT. The failure is silent, it is
ORDER-DEPENDENT (so a full suite can pass and then fail on the same tree), and
the symptom -- "this script times out" -- points at the script rather than at
the list. It is also exactly the class docs/perf/GATES.md section 3 is about: a
knob that reads as working, renders plausibly, and asserts nothing. Adding
HANABI_BIG_EVENTS to the rbig fixture on 2026-08-26 needed this line and very
nearly did not get it.

It parses rather than greps: the getenv calls are found inside build_seed's
brace range only, with strings and comments stripped, and a file whose braces
do not balance is reported as unreadable rather than confidently wrong.
"""

import re
import sys

PATH = "src/api/mock_client.h"


def strip_code(text):
    """Blank out comments and string literals, preserving line structure."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            # Keep the literal's TEXT: the env names we are looking for are
            # string literals, and blanking them is blanking the evidence.
            out.append(text[i:j])
            i = j
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            out.append(' ' * (min(j + 1, n) - i))
            i = min(j + 1, n)
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def brace_range(text, start):
    """[start_of_body, end_of_body) for the first {...} at or after `start`."""
    open_at = text.find('{', start)
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return (open_at + 1, i)
    return None


def main():
    try:
        raw = open(PATH, encoding='utf-8').read()
    except OSError as e:
        print("check_fixture_env: FAIL")
        print("  cannot read %s: %s" % (PATH, e))
        return 1
    text = strip_code(raw)

    m = re.search(r'kFixtureEnv\s*\[\s*\]\s*=', text)
    if not m:
        print("check_fixture_env: FAIL")
        print("  %s: no kFixtureEnv table found. It is the cache key for the" % PATH)
        print("  whole generated catalog; if it was renamed, rename it here too.")
        return 1
    listed = set(re.findall(r'"([A-Z0-9_]+)"', text[m.end():text.find(';', m.end())]))

    m = re.search(r'\bbuild_seed\s*\([^)]*\)', text)
    if not m:
        print("check_fixture_env: FAIL")
        print("  %s: no build_seed() found." % PATH)
        return 1
    rng = brace_range(text, m.end())
    if rng is None:
        print("check_fixture_env: FAIL")
        print("  %s: build_seed()'s braces do not balance, so this check" % PATH)
        print("  cannot say anything about it. That is a parse problem here or")
        print("  a real syntax error there; it is not a verdict either way.")
        return 1

    body = text[rng[0]:rng[1]]
    used = {}
    for gm in re.finditer(r'getenv\s*\(\s*"([A-Z0-9_]+)"', body):
        name = gm.group(1)
        if name not in used:
            used[name] = raw[:rng[0] + gm.start()].count('\n') + 1

    missing = sorted(n for n in used if n not in listed)
    if not missing:
        print("check_fixture_env: ok (%d read, %d listed)" % (len(used), len(listed)))
        return 0

    print("check_fixture_env: FAIL")
    for name in missing:
        print("  %s:%d: build_seed() reads %s and kFixtureEnv does not list it"
              % (PATH, used[name], name))
    print()
    print("  The generated catalog is built ONCE and cached, keyed on the")
    print("  variables in kFixtureEnv. A knob that is not in that list is read")
    print("  on the first call and never again -- so in the scripted UI runner,")
    print("  which loads a whole directory of scripts into one process and")
    print("  applies each script's own `# env:` line, whichever script runs")
    print("  first decides what every later script gets. The symptom is a")
    print("  script timing out waiting for text that was never going to")
    print("  appear, and it is ORDER-DEPENDENT: the same tree passes a full")
    print("  run and fails the next one.")
    print()
    print("  Add each name above to kFixtureEnv in %s." % PATH)
    return 1


if __name__ == "__main__":
    sys.exit(main())
