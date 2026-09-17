#!/usr/bin/env python3
"""The script's outcome, from the supervisor's VALIDATED record -- never from
the supervisor's exit code. One line on stdout:

    <result> <reason> <certain|uncertain> <root_pid> <record json>

absent / malformed / no `ownership` -> unsupervised no_record uncertain;
ownership != established or reap_timed_out -> unsupervised; wall_hit ->
timeout; interrupted -> aborted; term_signal -> fail (signal named);
exit_status 0 -> pass, else fail. Cleanup certain only for "none observed".
"""

import json
import sys


def classify(rec):
    if not isinstance(rec, dict) or "ownership" not in rec:
        return "unsupervised", "no_record", "uncertain"
    if rec.get("ownership") != "established" or rec.get("reap_timed_out"):
        why = str(rec.get("ownership")) + ("+reap_timed_out" if rec.get("reap_timed_out") else "")
        return "unsupervised", why, "uncertain"
    cleanup = "certain" if rec.get("cleanup") == "none observed" else "uncertain"
    if rec.get("wall_hit"):
        return "timeout", "wall", cleanup
    if rec.get("interrupted"):
        return "aborted", "interrupted", cleanup
    if rec.get("term_signal") is not None:
        return "fail", "signal " + str(rec["term_signal"]), cleanup
    if rec.get("exit_status") == 0:
        return "pass", "exit 0", cleanup
    return "fail", "exit " + str(rec.get("exit_status")), cleanup


def main(argv):
    try:
        with open(argv[1]) as f:
            rec = json.load(f)
    except (OSError, ValueError, IndexError):
        rec = {"cleanup": "uncertain", "ownership": "no_record"}
        result, why, cleanup = "unsupervised", "no_record", "uncertain"
        print(result, why, cleanup, 0, json.dumps(rec, sort_keys=True))
        return 0
    result, why, cleanup = classify(rec)
    if not isinstance(rec, dict):
        rec = {"cleanup": "uncertain", "ownership": "no_record"}
    print(result, why, cleanup, int(rec.get("root_pid") or 0), json.dumps(rec, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
