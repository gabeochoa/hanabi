"""The script's outcome, from the supervisor's VALIDATED record -- never from
the supervisor's exit code. One JSON object on one line -- a reason may
contain spaces ("exit 0", "signal 11"), so nothing here is ever split on
whitespace:

    {"result": ..., "reason": ..., "cleanup": "certain"|"uncertain",
     "root_pid": <int>, "record": <the record as read, or a no_record stub>}

usage: classify_record.py [--field result|reason|cleanup|root_pid|record]
                          <record path> [<expected token>]
--field prints that one value alone (record as JSON), for a caller that
wants no parsing at all.

absent / malformed / no `ownership` -> unsupervised no_record uncertain;
token expected and absent or different -> unsupervised token_mismatch;
ownership != established or reap_timed_out -> unsupervised; wall_hit ->
timeout; interrupted -> aborted; term_signal -> fail (signal named);
exit_status 0 -> pass, else fail. Cleanup certain only for "none observed"
with the observer "ok".
"""

import json
import sys


def classify(rec, token=None):
    if not isinstance(rec, dict) or "ownership" not in rec:
        return "unsupervised", "no_record", "uncertain"
    if token is not None and rec.get("token") != token:
        return "unsupervised", "token_mismatch", "uncertain"
    if rec.get("ownership") != "established" or rec.get("reap_timed_out"):
        why = str(rec.get("ownership")) + ("+reap_timed_out" if rec.get("reap_timed_out") else "")
        return "unsupervised", why, "uncertain"
    cleanup = ("certain" if rec.get("cleanup") == "none observed" and rec.get("observer", "ok") == "ok"
               else "uncertain")
    if rec.get("wall_hit"):
        return "timeout", "wall", cleanup
    if rec.get("interrupted"):
        return "aborted", "interrupted", cleanup
    if rec.get("term_signal") is not None:
        return "fail", "signal " + str(rec["term_signal"]), cleanup
    if rec.get("exit_status") == 0:
        return "pass", "exit 0", cleanup
    return "fail", "exit " + str(rec.get("exit_status")), cleanup


FIELDS = ("result", "reason", "cleanup", "root_pid", "record")


def verdict(path, token=None):
    try:
        with open(path) as f:
            rec = json.load(f)
    except (OSError, ValueError):
        rec = {"cleanup": "uncertain", "ownership": "no_record"}
        result, why, cleanup = "unsupervised", "no_record", "uncertain"
    else:
        result, why, cleanup = classify(rec, token)
        if not isinstance(rec, dict):
            rec = {"cleanup": "uncertain", "ownership": "no_record"}
    root_pid = rec.get("root_pid") if isinstance(rec, dict) else 0
    if not isinstance(root_pid, int):
        root_pid = 0
    return {"result": result, "reason": why, "cleanup": cleanup, "root_pid": root_pid, "record": rec}


def main(argv):
    args = list(argv[1:])
    field = None
    if len(args) >= 2 and args[0] == "--field":
        field = args[1]
        if field not in FIELDS:
            sys.stderr.write("classify_record.py: unknown --field %r\n" % field)
            return 64
        args = args[2:]
    if not args:
        sys.stderr.write("usage: classify_record.py [--field NAME] <record path> [<expected token>]\n")
        return 64
    v = verdict(args[0], args[1] if len(args) > 1 else None)
    if field is None:
        print(json.dumps(v, sort_keys=True))
    elif field == "record":
        print(json.dumps(v["record"], sort_keys=True))
    else:
        print(v[field])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
