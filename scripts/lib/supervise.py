#!/usr/bin/env python3
"""Owned-process supervisor for one scripted-UI fixture.

Runs ONE child in its own session (setsid: pgid == pid), keeps that child
UNREAPED until every teardown signal has been sent -- so the kernel holds its
pid and its group id for the whole time -- signals only the GROUP, never a pid
number, then performs exactly one wait and reports the child's real exit and
the cleanup's facts as SEPARATE things in a JSON record.

    supervise.py --wall <s> --grace <s> --record <path> [--log <path>]
                 [--token <str>] -- <cmd...>

Exit status: the child's (0-255) when it exited; 124 when the wall clock hit;
130 when the supervisor itself was told to stop; 128+SIGNUM when the child
died of a signal; 78 (EX_CONFIG) when ownership could not be established or
the leader could not be reaped -- in both cases NOTHING is signalled that is
not provably ours, nothing blocks, and the record says what was left.

Record fields: token (the runner's invocation token, echoed; the record
is written atomically so a partial file never carries one), root_pid, pgid, ownership (established | unestablished |
sigchld_not_default | spawn_failed), exit_status, term_signal, wall_hit, tree_closed,
pipe_closed_early (the inherited pipe hit EOF while the leader lived: a
hint that woke the loop, never a decision), root_exited, interrupted,
term_sent, kill_sent, members_at_term, members_at_kill,
left_in_group, escaped_group (chained to the root by ppid, in another
group; the session cannot be told portably, so session is "unknown"),
escaped_session (null: no portable session keyword, so never observed), census
("observed": the lists are what ONE ps snapshot after the final signal
showed -- a descendant that called setsid itself and was reparented to init
is untraceable by ppid and stays unreported, by design; "failed: <reason>"
with the lists null when ps could not be read), observer ("ok" | "failed:
ps" once any single-pid read failed -- the read is then taken as "alive",
which only ever sends a group signal or waits longer), bounds {wall, grace,
ps_timeout} (total runtime <= wall + grace + 2 x ps_timeout + a tick),
cleanup ("none observed" | uncertain; uncertain whenever the observer or
the census failed), and on the error arms action / reap_timed_out.

The CLI binds the real operating system, unconditionally. `Ops` exists so the
unit tests can drive the same control flow with fakes; no environment
variable, flag or fixture line selects them.

Portability: targets the Mac's /usr/bin/python3 (3.9.6) and Linux 3.12 --
stdlib only (argparse, json, os, select, signal, subprocess, sys, time),
Popen kwargs limited to stdin/stdout/stderr, start_new_session, pass_fds,
close_fds (all present in 3.9), ps spellings shared by BSD ps and procps.
Syntax is checked against 3.9 by the unit; 3.9.6 RUNTIME behaviour is
unverified until an authorized Mac run of the synthetic unit.
"""

import argparse
import json
import os
import select
import signal
import subprocess
import sys
import time

assert sys.version_info >= (3, 9), "supervise.py targets the Mac's /usr/bin/python3 (3.9)"


class Ops:
    """The operating system, as this module uses it."""

    _proc = None  # the managed child's Popen, held for the supervisor's life

    def spawn(self, cmd, log_fd, tree_fd):  # -> pid, or None when exec failed
        # The object is never polled, waited, communicated with or read for
        # returncode before teardown, so it never enters subprocess._active
        # and nothing but our one waitpid can reap the child.
        try:
            # No env= dict, no shell: the argv carries its own `env` prefix,
            # so the fixture's variables are built by env(1) for the app alone.
            proc = subprocess.Popen(
                cmd, stdin=subprocess.DEVNULL, stdout=log_fd, stderr=log_fd,
                start_new_session=True, pass_fds=(tree_fd,), close_fds=True)
        except OSError:
            return None  # CPython already waited the failed fork/exec child
        self._proc = proc
        return proc.pid

    def getpgid(self, pid):
        return os.getpgid(pid)

    def killpg(self, pgid, sig):
        os.killpg(pgid, sig)

    def waitpid(self, pid):
        """The ONE reap site, never blocking: None when the child is not yet
        reapable, else (exit_status, term_signal) from the wait status, never
        from the Popen object."""
        got, status = os.waitpid(pid, os.WNOHANG)
        if got == 0:
            return None
        code = os.waitstatus_to_exitcode(status)
        # Reconcile the retained object so its finaliser cannot issue a
        # second wait against a now-reusable number.
        if self._proc is not None and self._proc.pid == pid:
            self._proc.returncode = code
        return (code, None) if code >= 0 else (None, -code)

    # `ps` is run through libc system(3), never subprocess: constructing a
    # second Popen in this process would reap our dead-but-unreaped leader
    # from CPython's `_active` list and free its pid (I1). system() waits
    # only for the child it forked.
    @staticmethod
    def _ps(args, timeout):  # -> (rc, text); only BSD/procps-common spellings
        # subprocess.run waits for the pid IT created and nothing else, so
        # the managed child stays ours to reap; no shell, no file. A hung ps
        # (the runner's known hazard) is cut at `timeout`.
        try:
            done = subprocess.run(["ps"] + args.split(), stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, check=False,
                                  timeout=max(0.05, timeout))
        except (OSError, subprocess.SubprocessError):
            return 1, ""
        return done.returncode, done.stdout.decode("utf-8", "replace")

    def census(self, timeout):
        """-> [(pid, ppid, pgid, stat)] or None when ps failed or printed a
        line this parser cannot read: an honest "unknown", never an empty
        list that would read as clean."""
        rc, out = self._ps("-ax -o pid=,ppid=,pgid=,stat=", timeout)
        if rc != 0:
            return None
        rows = []
        for line in out.splitlines():
            if not line.strip():
                continue
            parts = line.split(None, 3)
            if len(parts) < 4:
                return None
            try:
                rows.append((int(parts[0]), int(parts[1]), int(parts[2]), parts[3]))
            except ValueError:
                return None
        return rows

    def is_zombie(self, pid, timeout):
        """True / False / None (ps failed or said nothing about a pid we hold).
        `ps -p` describes OUR unreaped child: the pid is reserved while it is
        a zombie, so this can name no stranger."""
        rc, out = self._ps("-o stat= -p " + str(int(pid)), timeout)
        out = out.strip()
        if rc != 0 or not out:
            return None
        return out.startswith("Z")

    def monotonic(self):
        return time.monotonic()

    def sleep(self, seconds):
        time.sleep(seconds)

    def sigchld_disposition(self):
        return signal.getsignal(signal.SIGCHLD)

    def reset_sigchld(self):
        signal.signal(signal.SIGCHLD, signal.SIG_DFL)

    def pipe(self):
        return os.pipe()

    def tree_closed(self, read_fd, timeout):
        """True when every holder of the write end has closed it (EOF)."""
        r, _, _ = select.select([read_fd], [], [], max(0.0, timeout))
        if not r:
            return False
        try:
            return os.read(read_fd, 1) == b""
        except OSError:
            return True

    def close(self, fd):
        try:
            os.close(fd)
        except OSError:
            pass


def sigchld_ok(disposition):
    """SIG_DFL keeps a dead child as a zombie until we wait; SIG_IGN (and a
    handler) may reap it behind our back and free the pid. Read AFTER our own
    write; before it the read cannot see NOCLDWAIT."""
    return disposition == signal.SIG_DFL


def members(census, pgid, root):
    """Live (non-zombie) processes in our group, the root excluded."""
    return sorted(p for p, _pp, pg, st in census
                  if pg == pgid and p != root and not st.startswith("Z"))


def escaped(census, root, pgid):
    """Processes whose ppid chain reaches the root but whose pgid is not ours:
    they left the session (setsid/daemonised) and cannot be signalled by the
    group. Reported, never touched."""
    parent = {p: pp for p, pp, _pg, _st in census}
    group = {p: pg for p, _pp, pg, _st in census}
    out = []
    for p in parent:
        if p == root or group.get(p) == pgid:
            continue
        hops, q = 0, p
        while q in parent and hops <= len(parent):
            q = parent[q]
            hops += 1
            if q == root:
                out.append(p)
                break
            if q <= 1:
                break
    return sorted(out)


class Interrupted:
    """Set by the CLI's SIGTERM/SIGINT handler; read by the wait loop so our
    own death takes the ordinary teardown path."""

    flag = False


PS_TIMEOUT = 10.0


def supervise(cmd, wall, grace, log_fd, ops, tick=0.1, interrupted=None, ps_timeout=PS_TIMEOUT,
              token=""):
    interrupted = interrupted or Interrupted()
    rec = {
        "token": token,  # binds this record to the invocation that asked for it
        "root_pid": None, "pgid": None, "ownership": "unestablished",
        "exit_status": None, "term_signal": None, "wall_hit": False,
        "tree_closed": False, "pipe_closed_early": False, "root_exited": False,
        "interrupted": False, "term_sent": False, "kill_sent": False,
        "members_at_term": [], "members_at_kill": [], "left_in_group": [],
        "escaped_group": [], "escaped_session": None, "session": "unknown",
        "census": "observed", "observer": "ok", "cleanup": "uncertain",
        "bounds": {"wall": wall, "grace": grace, "ps_timeout": ps_timeout},
        "portability": "syntax-checked against 3.9; runtime unverified until an "
                       "authorized Mac synthetic-unit run",
    }
    # Always WRITTEN, never merely read: getsignal() cannot see an inherited
    # SA_NOCLDWAIT, and sigaction replaces the whole disposition -- handler
    # and flags -- so after this write SIG_DFL is a true statement about both.
    ops.reset_sigchld()
    rec["sigchld_reset"] = True
    if not sigchld_ok(ops.sigchld_disposition()):
        rec["ownership"] = "sigchld_not_default"
        return rec, 70
    read_fd, write_fd = ops.pipe()
    pid = ops.spawn(cmd, log_fd, write_fd)
    ops.close(write_fd)  # the tree alone holds the write end now
    if pid is None:
        rec["ownership"] = "spawn_failed"
        ops.close(read_fd)
        return rec, 78
    rec["root_pid"] = pid
    try:
        pgid = ops.getpgid(pid)
    except OSError:
        pgid = None
    if pgid != pid:
        # Not a session of its own: nothing we could signal is provably ours,
        # and a blocking wait on a live child would hang. Left running,
        # unsignalled, unreaped (the pid stays reserved until we exit and it
        # is reparented); the runner marks the cleanup uncertain.
        rec["pgid"] = pgid
        rec["ownership"] = "unestablished"
        rec["action"] = ("left running unsignalled; not reaped (pid reserved until this "
                         "supervisor exits); exit 78")
        ops.close(read_fd)
        return rec, 78
    rec["pgid"] = pgid
    rec["ownership"] = "established"

    # Normal completion is ONE fact: the unreaped leader observed exited
    # (a zombie; its pid still ours). EOF on the inherited pipe only wakes
    # the loop to look again -- the app makes no promise to hold an unknown
    # fd, so a closed pipe with a live leader is noted and waited through.
    # Only the wall clock (or our own interruption) starts a teardown on a
    # live leader.
    deadline = ops.monotonic() + wall

    # The observer, tri-state: True (exited), False (alive), None (ps failed
    # -- recorded once; callers treat it as "assume alive", which only ever
    # sends a GROUP signal or waits longer). Each call is cut at the smaller
    # of ps_timeout and the time left to `until`, so one hung ps cannot push
    # a loop past its bound by more than one call.
    def observe(until):
        z = ops.is_zombie(pid, max(0.05, min(ps_timeout, until - ops.monotonic())))
        if z is None and rec["observer"] == "ok":
            rec["observer"] = "failed: ps"
        return z

    root_exited = False
    pipe_open = True
    while True:
        if interrupted.flag:
            rec["interrupted"] = True
            break
        if ops.monotonic() >= deadline:
            rec["wall_hit"] = True
            break
        if observe(deadline) is True:
            root_exited = True
            break
        remaining = deadline - ops.monotonic()
        if remaining <= 0:
            rec["wall_hit"] = True
            break
        if pipe_open:
            if ops.tree_closed(read_fd, min(tick, remaining)):
                pipe_open = False
                rec["tree_closed"] = True
                if observe(deadline) is True:
                    root_exited = True
                    break
                rec["pipe_closed_early"] = True
        else:
            ops.sleep(min(tick, remaining))
    ops.close(read_fd)
    rec["root_exited"] = root_exited

    # Teardown while the leader is still unreaped: the group id is held. A
    # census that fails is UNKNOWN: signals still go to the group (that needs
    # no census), the report says the census failed, and nothing is clean.
    census_failed = False
    teardown_end = ops.monotonic() + grace + 2 * ps_timeout

    def look():
        nonlocal census_failed
        snap = ops.census(max(0.05, min(ps_timeout, teardown_end - ops.monotonic())))
        if snap is None:
            census_failed = True
            return None
        return snap

    snapshot = look()
    live = members(snapshot, pgid, pid) if snapshot is not None else []
    if rec["wall_hit"] or rec["interrupted"] or live or (snapshot is None and not root_exited):
        rec["members_at_term"] = live if snapshot is not None else None
        ops.killpg(pgid, signal.SIGTERM)
        rec["term_sent"] = True
        end = ops.monotonic() + grace
        while ops.monotonic() < end:
            snapshot = look()
            if snapshot is not None and not members(snapshot, pgid, pid) and observe(end) is True:
                break
            if ops.monotonic() >= end:
                break
            ops.sleep(tick)
        snapshot = look()
        survivors = members(snapshot, pgid, pid) if snapshot is not None else []
        if survivors or snapshot is None or observe(teardown_end) is not True:
            rec["members_at_kill"] = survivors if snapshot is not None else None
            ops.killpg(pgid, signal.SIGKILL)
            rec["kill_sent"] = True
            ops.sleep(tick)
            snapshot = look()
    if snapshot is None:
        rec["census"] = "failed: ps unavailable or unparsable" + (" (during teardown)" if census_failed else "")
        rec["left_in_group"] = None
        rec["escaped_group"] = None
        rec["escaped_session"] = None
        rec["cleanup"] = "uncertain"
    else:
        rec["left_in_group"] = members(snapshot, pgid, pid)
        # Chained to the root by ppid but in another group. Without a
        # portable session keyword the two cannot be told apart, so every
        # such process is reported under escaped_group with session unknown.
        rec["escaped_group"] = escaped(snapshot, pid, pgid)
        rec["escaped_session"] = None  # no portable session keyword: not observed
        rec["session"] = "unknown"
        rec["cleanup"] = ("none observed" if not rec["left_in_group"] and not rec["escaped_group"]
                          and rec["observer"] == "ok" else "uncertain")

    # The one reap, after every signal, NEVER blocking: a ps `Z` reading is
    # weaker than waitability, and the bound must be the supervisor's own.
    # A leader still not reapable within the grace is recorded, not waited
    # for.
    reaped = None
    end = ops.monotonic() + grace
    while True:
        reaped = ops.waitpid(pid)
        if reaped is not None or ops.monotonic() >= end:
            break
        ops.sleep(tick)
    if reaped is None:
        rec["reap_timed_out"] = True
        rec["cleanup"] = "uncertain"
        return rec, 78
    st, sig = reaped
    rec["exit_status"], rec["term_signal"] = st, sig
    if rec["interrupted"]:
        return rec, 130
    if rec["wall_hit"]:
        return rec, 124
    if st is not None:
        return rec, st
    return rec, 128 + (sig or 0)


def write_record(path, rec):
    """Atomic: a partial file can never carry a valid token."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(rec, f, sort_keys=True)
        f.write("\n")
    os.replace(tmp, path)


def main(argv):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--wall", type=float, required=True)
    ap.add_argument("--grace", type=float, required=True)
    ap.add_argument("--record", required=True)
    ap.add_argument("--log", default=None)
    ap.add_argument("--token", default="")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args(argv)
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        sys.stderr.write("supervise: no command\n")
        return 2
    log_fd = os.open(a.log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600) if a.log else 1
    ops = Ops()
    interrupted = Interrupted()

    def on_term(_signum, _frame):
        # Our own death is a teardown, not an escape: the wait loop sees the
        # flag and takes the ordinary TERM -> grace -> KILL -> wait path.
        interrupted.flag = True

    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)
    signal.signal(signal.SIGHUP, on_term)
    rec, status = supervise(cmd, a.wall, a.grace, log_fd, ops, interrupted=interrupted,
                            token=a.token)
    rec["token"] = a.token
    write_record(a.record, rec)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
