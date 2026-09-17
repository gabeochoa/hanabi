#!/usr/bin/env python3
"""Isolated unit for scripts/lib/classify_record.py: classify(rec, token) over
dicts. No shell, no process, no file. The runner's verdict ladder, case by
case, plus the two stale-record arms.

Run: python3 tests/runner_policy/test_classify_record.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "lib"))
from classify_record import classify  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    if cond:
        print("ok   " + what)
    else:
        failures += 1
        print("FAIL " + what)


TOKEN = "run:script:12345:1789657000"


def est(**kw):
    rec = {"token": TOKEN, "ownership": "established", "cleanup": "none observed",
           "observer": "ok", "exit_status": None, "term_signal": None}
    rec.update(kw)
    return rec


# The nine ladder cases.
check(classify(est(exit_status=0, reap_timed_out=True), TOKEN) == ("unsupervised", "established+reap_timed_out", "uncertain"),
      "a passed fixture whose reap timed out is unsupervised, uncertain")
check(classify({"not": "a record"}, TOKEN) == ("unsupervised", "no_record", "uncertain"),
      "a record without ownership is no_record")
check(classify(None, TOKEN) == ("unsupervised", "no_record", "uncertain"), "a non-dict is no_record")
check(classify({"token": TOKEN, "ownership": "unestablished"}, TOKEN) == ("unsupervised", "unestablished", "uncertain"),
      "ownership unestablished is unsupervised")
check(classify({"token": TOKEN, "ownership": "spawn_failed"}, TOKEN)[0] == "unsupervised", "spawn failed is unsupervised")
check(classify({"token": TOKEN, "ownership": "sigchld_not_default"}, TOKEN)[0] == "unsupervised",
      "SIGCHLD not default is unsupervised")
check(classify(est(wall_hit=True, term_signal=15), TOKEN) == ("timeout", "wall", "certain"),
      "wall hit is timeout even though the child died of a signal")
check(classify(est(interrupted=True, term_signal=15), TOKEN) == ("aborted", "interrupted", "certain"),
      "supervisor interrupted is aborted")
check(classify(est(term_signal=11), TOKEN) == ("fail", "signal 11", "certain"), "a child killed by a signal is fail, signal named")
check(classify(est(exit_status=5, cleanup="uncertain", left_in_group=[7]), TOKEN) == ("fail", "exit 5", "uncertain"),
      "exit 5 is fail; a leftover is uncertain")
check(classify(est(exit_status=0), TOKEN) == ("pass", "exit 0", "certain"), "exit 0 with nothing observed is pass, certain")
check(classify(est(exit_status=0, observer="failed: ps"), TOKEN) == ("pass", "exit 0", "uncertain"),
      "a failed observer makes a pass uncertain, not a failure")
check(classify(est(exit_status=0, cleanup="uncertain", escaped_group=[9]), TOKEN) == ("pass", "exit 0", "uncertain"),
      "an escaped process makes a pass uncertain")

# The stale-record arms.
check(classify(est(exit_status=0, token="run:script:99999:1"), TOKEN) == ("unsupervised", "token_mismatch", "uncertain"),
      "a PASS record from another invocation is token_mismatch, never a pass")
old = est(exit_status=0)
del old["token"]
check(classify(old, TOKEN) == ("unsupervised", "token_mismatch", "uncertain"), "a record with no token is token_mismatch")
check(classify(est(exit_status=0), None) == ("pass", "exit 0", "certain"), "with no expected token the token is not checked")

if failures:
    print("%d failure(s)" % failures)
    sys.exit(1)
print("classify_record unit ok")
