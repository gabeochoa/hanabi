#!/usr/bin/env python3
"""Deterministic unit for scripts/lib/supervise.py: every operating-system
operation is an injected fake, so no process is spawned, no signal is sent
and no clock is waited on. The fakes record the ORDER of calls, which is what
the ownership argument rests on: signals go to the group while the leader is
unreaped; the single wait comes last.

Run: python3 tests/runner_policy/test_supervise.py
"""

import os
import signal
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "lib"))
import supervise  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    if cond:
        print("ok   " + what)
    else:
        failures += 1
        print("FAIL " + what)


class World:
    """A fake OS with a clocked timeline. The leader becomes a zombie at
    `root_dies_at` (clock seconds) or when a signal named in `root_dies_on`
    reaches its group; group members follow `member_dies_on`. `census()`
    derives from that state -- no frame scripting -- so every observation
    the supervisor makes is consistent with every other one."""

    def __init__(self, pid=500, pgid=None, sigchld=signal.SIG_DFL, reset_works=True,
                 root_dies_at=None, root_dies_on=("TERM", "KILL"), members=(), member_dies_on=("TERM", "KILL"),
                 stuck_members=(), escaped=(), strangers=(), tree_closes_at=None,
                 reapable=True, exit_code=0, census_fails=False):
        self.pid = pid
        self.pgid = pid if pgid is None else pgid
        self.sigchld = sigchld
        self.reset_works = reset_works
        self.root_dies_at = root_dies_at
        self.root_dies_on = set(root_dies_on)
        self.members = set(members)          # live pids in our group
        self.member_dies_on = set(member_dies_on)
        self.stuck_members = set(stuck_members)  # survive everything
        self.escaped = list(escaped)         # (pid, ppid, pgid) outside our group, chained to root
        self.strangers = list(strangers)     # (pid, ppid, pgid) unrelated
        self.tree_closes_at = tree_closes_at
        self.reapable = reapable
        self.exit_code = exit_code
        self.census_fails = census_fails
        self.term_signal = None
        self.calls = []
        self.clock = 0.0
        self.root_dead = False
        self.tree_polls = 0

    def _tick_root(self):
        if self.root_dies_at is not None and self.clock >= self.root_dies_at:
            self.root_dead = True

    # -- Ops surface --------------------------------------------------------
    def sigchld_disposition(self):
        return self.sigchld

    def reset_sigchld(self):
        self.calls.append("reset_sigchld")
        if self.reset_works:
            self.sigchld = signal.SIG_DFL

    def pipe(self):
        return (10, 11)

    def spawn(self, cmd, log_fd, tree_fd):
        self.calls.append(("spawn", tuple(cmd), tree_fd))
        return self.pid

    def close(self, fd):
        self.calls.append(("close", fd))

    def getpgid(self, pid):
        return self.pgid

    def killpg(self, pgid, sig):
        self.calls.append(("killpg", pgid, sig))
        name = {signal.SIGTERM: "TERM", signal.SIGKILL: "KILL"}[sig]
        if pgid == self.pgid:
            if name in self.member_dies_on:
                self.members -= (self.members - self.stuck_members)
            if not self.root_dead and name in self.root_dies_on:
                self.root_dead = True
                self.term_signal = sig

    def waitpid(self, pid):
        self.calls.append(("waitpid", pid))
        if not self.reapable:
            return None
        if self.term_signal is not None:
            return (None, self.term_signal)
        return (self.exit_code, None)

    def census(self):
        self.calls.append("census")
        self._tick_root()
        if self.census_fails:
            return None
        rows = [(self.pid, 1, self.pgid, "Z" if self.root_dead else "S")]
        rows += [(m, self.pid, self.pgid, "D" if m in self.stuck_members else "S") for m in sorted(self.members)]
        rows += [(p, pp, pg, "S") for p, pp, pg in self.escaped]
        rows += [(p, pp, pg, "S") for p, pp, pg in self.strangers]
        return rows

    def is_zombie(self, pid):
        self.calls.append("is_zombie")
        self._tick_root()
        return self.root_dead

    def monotonic(self):
        return self.clock

    def sleep(self, seconds):
        self.clock += seconds

    def tree_closed(self, read_fd, timeout):
        self.tree_polls += 1
        self.clock += timeout
        return self.tree_closes_at is not None and self.tree_polls >= self.tree_closes_at


def run(world, wall=10.0, grace=2.0):
    return supervise.supervise(["app", "--e2e", "x"], wall, grace, 1, world, tick=0.5)


def signals(world):
    return [c for c in world.calls if isinstance(c, tuple) and c[0] == "killpg"]


def waits(world):
    return [c for c in world.calls if isinstance(c, tuple) and c[0] == "waitpid"]


def index_of(world, pred):
    for i, c in enumerate(world.calls):
        if pred(c):
            return i
    return -1


ROOT = 500


# T1: normal exit before the wall: the leader exits (observed Z), the group is
# empty -> no signal at all, one blocking wait, the child's exit code.
def t1():
    w = World(root_dies_at=1.0, exit_code=3)
    rec, st = run(w)
    check(st == 3 and rec["exit_status"] == 3, "T1 exit status is the child's, via the one wait")
    check(rec["root_exited"] and not rec["wall_hit"], "T1 the root's exit ended the wait, wall not hit")
    check(signals(w) == [], "T1 no signal sent on a clean exit")
    check(waits(w) == [("waitpid", ROOT)], "T1 exactly one wait, non-blocking, succeeded")
    check(rec["cleanup"] == "none observed" and rec["census"] == "observed", "T1 cleanup none observed, census marked observed")
    check(index_of(w, lambda c: c == ("close", 11)) < index_of(w, lambda c: c == "census"),
          "T1 our copy of the write end closes before any census")


# T2: wall hit; TERM to the GROUP; leader and member die of it; no KILL; wait.
def t2():
    w = World(members=(501,))
    rec, st = run(w, wall=1.0)
    check(rec["wall_hit"] and st == 124, "T2 wall hit -> 124")
    check(signals(w) == [("killpg", ROOT, signal.SIGTERM)], "T2 TERM to the group id == root pid, no KILL")
    check(rec["members_at_term"] == [501] and rec["members_at_kill"] == [], "T2 members recorded at TERM only")
    kill_i = index_of(w, lambda c: isinstance(c, tuple) and c[0] == "killpg")
    wait_i = index_of(w, lambda c: isinstance(c, tuple) and c[0] == "waitpid")
    check(0 <= kill_i < wait_i, "T2 the wait comes after the signal")
    check(rec["term_signal"] == signal.SIGTERM and rec["exit_status"] is None, "T2 died of TERM, recorded as such")


# T3: the tree ignores TERM -> KILL to the group; then nothing left.
def t3():
    w = World(members=(501,), root_dies_on=("KILL",), member_dies_on=("KILL",))
    rec, st = run(w, wall=1.0, grace=1.0)
    check([c[2] for c in signals(w)] == [signal.SIGTERM, signal.SIGKILL], "T3 TERM then KILL, both to the group")
    check(all(c[1] == ROOT for c in signals(w)), "T3 every signal names the group id (== root pid), never a member pid")
    check(rec["kill_sent"] and rec["left_in_group"] == [], "T3 KILL sent; nothing left in the group")
    check(waits(w) == [("waitpid", ROOT)], "T3 one wait after KILL")
    check(rec["cleanup"] == "none observed", "T3 cleanup none observed")


# T4: a member survives even KILL (uninterruptible) -> left_in_group, UNCERTAIN.
def t4():
    w = World(members=(501, 502), stuck_members=(502,))
    rec, st = run(w, wall=1.0, grace=0.5)
    check(rec["kill_sent"], "T4 KILL was sent (a member survived TERM)")
    check(rec["left_in_group"] == [502] and rec["cleanup"] == "uncertain", "T4 survivor observed; cleanup UNCERTAIN")
    check(st == 124, "T4 the test outcome (timeout) is separate from the cleanup fact")


# T5: descendants that left the session (pgid != ours) but whose ppid chain
# reaches the root: reported as escaped, never in a killpg argument.
def t5():
    w = World(root_dies_at=1.0, escaped=((600, ROOT, 600), (601, 600, 600)))
    rec, st = run(w)
    check(rec["escaped_group"] == [600, 601] and rec["session"] == "unknown",
          "T5 descendants in another group observed by ppid chain; session not claimed")
    check(signals(w) == [], "T5 nothing signalled: the escaped processes are outside our group, the group was empty")
    check(rec["cleanup"] == "uncertain", "T5 escaped -> UNCERTAIN, not clean")


# T6: a stranger holding a recycled pid number appears with pgid != ours and
# no chain to the root: not counted, not signalled.
def t6():
    w = World(root_dies_at=1.0, strangers=((501, 1, 700),))
    rec, st = run(w)
    check(rec["left_in_group"] == [] and rec["escaped_group"] == [], "T6 stranger neither in-group nor escaped")
    check(signals(w) == [], "T6 stranger untouched")
    check(rec["cleanup"] == "none observed" and rec["escaped_session"] is None,
          "T6 none observed; escaped_session not claimed (no portable session read)")


# T7: the supervisor itself is told to stop mid-run -> the same teardown path,
# record written with interrupted=true, exit 130.
def t7():
    w = World(members=(501,))
    flag = supervise.Interrupted()
    flag.flag = True
    rec, st = supervise.supervise(["app"], 10.0, 1.0, 1, w, tick=0.5, interrupted=flag)
    check(rec["interrupted"] and st == 130, "T7 interrupted -> 130")
    check([c[2] for c in signals(w)] == [signal.SIGTERM], "T7 teardown TERM went to the group")
    check(len(waits(w)) == 1, "T7 one wait")


# T8: SIGCHLD preflight: an inherited SIG_IGN is reset to SIG_DFL before the
# spawn; a reset that does not take -> no spawn, no signal, ownership refused.
def t8():
    w = World(sigchld=signal.SIG_IGN, root_dies_at=0.5)
    rec, st = run(w)
    spawn_i = index_of(w, lambda c: isinstance(c, tuple) and c[0] == "spawn")
    check(w.calls[0] == "reset_sigchld" and spawn_i > 0, "T8 SIG_DFL WRITTEN before the spawn (SIG_IGN inherited)")
    check(rec["ownership"] == "established" and rec["sigchld_reset"], "T8 with the reset taken, ownership is established; reset recorded")
    # Written even when the read already says SIG_DFL: the read cannot see
    # an inherited SA_NOCLDWAIT flag; the write clears it.
    w_dfl = World(sigchld=signal.SIG_DFL, root_dies_at=0.5)
    rec_dfl, _ = run(w_dfl)
    check(w_dfl.calls[0] == "reset_sigchld" and rec_dfl["sigchld_reset"], "T8 the write happens unconditionally")
    w2 = World(sigchld=signal.SIG_IGN, reset_works=False)
    rec2, st2 = run(w2)
    check(rec2["ownership"] == "sigchld_not_default" and st2 == 70, "T8 reset refused -> 70")
    check(index_of(w2, lambda c: isinstance(c, tuple) and c[0] == "spawn") == -1, "T8 nothing spawned")
    check(signals(w2) == [] and waits(w2) == [], "T8 nothing signalled, nothing waited")


# T9: getpgid != pid (setsid did not take): NO signal, NO blocking wait, the
# child left running and unreaped, exit 78, ownership unestablished.
def t9():
    w = World(pgid=1)
    rec, st = run(w)
    check(st == 78 and rec["ownership"] == "unestablished", "T9 ownership unestablished -> 78")
    check(signals(w) == [] and waits(w) == [], "T9 nothing signalled, nothing waited (no hang)")
    check("left running unsignalled" in rec.get("action", ""), "T9 the record says what was left")
    check(rec["pgid"] == 1 and rec["root_pid"] == ROOT, "T9 observed pgid and root pid recorded")


# T10: the leader observed exited (Z) but not yet reapable: bounded
# non-blocking waits, then reap_timed_out, exit 78, UNCERTAIN. No path may
# block: the fake has no blocking wait at all.
def t10():
    w = World(root_dies_at=0.5, reapable=False)
    rec, st = run(w, grace=1.0)
    check(signals(w) == [], "T10 nothing to signal")
    check(len(waits(w)) >= 2, "T10 bounded repeated non-blocking waits (Z is weaker than waitability)")
    check(rec.get("reap_timed_out") and st == 78 and rec["cleanup"] == "uncertain",
          "T10 leader not reapable -> reap_timed_out, 78, UNCERTAIN")
    # After a KILL the root may sit in uninterruptible I/O (never Z): the
    # same bounded loop, never a hang.
    w3 = World(root_dies_on=(), member_dies_on=(), members=(), reapable=False)
    rec3, st3 = run(w3, wall=1.0, grace=1.0)
    check(rec3["kill_sent"], "T10c KILL was sent to the group")
    check(len(waits(w3)) >= 2 and rec3.get("reap_timed_out") and st3 == 78,
          "T10c root never Z after KILL -> bounded non-blocking waits, reap_timed_out, 78")


# T11: Popen raised after fork (exec failure): CPython already waited that
# child -> spawn_failed, no signal, no wait, 78.
def t11():
    class NoSpawn(World):
        def spawn(self, cmd, log_fd, tree_fd):
            self.calls.append(("spawn", tuple(cmd), tree_fd))
            return None
    w = NoSpawn()
    rec, st = run(w)
    check(rec["ownership"] == "spawn_failed" and st == 78, "T11 spawn failure -> spawn_failed, 78")
    check(signals(w) == [] and waits(w) == [], "T11 nothing signalled, nothing waited")


# T12: the managed Popen object, as the real Ops holds it: no method is
# called on it before teardown, and returncode is reconciled right after the
# one waitpid. Exercised through the real Ops class with the subprocess and
# os primitives substituted -- no process is created.
def t12():
    import subprocess as sp
    calls = []

    class FakePopen:
        pid = 4242
        returncode = None

        def __init__(self, *a, **k):
            calls.append("construct")

        def __getattr__(self, name):  # any method touch is recorded
            calls.append(name)
            raise AssertionError("managed Popen touched: " + name)

    real_popen, real_waitpid, real_w2e = sp.Popen, os.waitpid, os.waitstatus_to_exitcode
    sp.Popen = FakePopen
    os.waitpid = lambda pid, flags: (pid, 0)  # exited 0
    os.waitstatus_to_exitcode = lambda status: 7
    try:
        ops = supervise.Ops()
        pid = ops.spawn(["x"], 1, 11)
        check(pid == 4242 and calls == ["construct"], "T12 spawn constructs the Popen and touches nothing else")
        # A second, unrelated Popen construction (a census helper would do
        # this) must not poll the managed child.
        FakePopen()
        check(calls == ["construct", "construct"], "T12 another Popen does not poll the managed child")
        got = ops.waitpid(4242)
        check(got == (7, None), "T12 the one waitpid yields the exit code from the wait status")
        check(ops._proc.returncode == 7, "T12 returncode reconciled right after the wait")
        check(calls == ["construct", "construct"], "T12 still no method called on the managed object")
    finally:
        sp.Popen, os.waitpid, os.waitstatus_to_exitcode = real_popen, real_waitpid, real_w2e


# T13: the pipe closes while the root still runs: no signal, no wait, the
# loop keeps polling the observer until it reads Z; the record notes the
# early close; exit status from the one wait.
def t13():
    w = World(root_dies_at=3.0, tree_closes_at=1, exit_code=2)
    rec, st = run(w, wall=10.0)
    check(rec["tree_closed"] and rec["pipe_closed_early"], "T13 EOF with a live root is recorded as an early close")
    check(not rec["wall_hit"] and rec["root_exited"], "T13 the root's own exit ended the wait, not the wall")
    check(signals(w) == [], "T13 no signal on EOF")
    zi = [i for i, c in enumerate(w.calls) if c == "is_zombie"]
    wi = index_of(w, lambda c: isinstance(c, tuple) and c[0] == "waitpid")
    check(len(zi) >= 3 and wi > zi[-1], "T13 the wait came only after the observer read Z")
    check(st == 2 and rec["exit_status"] == 2, "T13 exit status from the one wait")


# T14: the pipe closes and the root is Z on the same tick: the normal path,
# not an early close.
def t14():
    w = World(root_dies_at=0.5, tree_closes_at=1, exit_code=0)
    rec, st = run(w)
    check(rec["tree_closed"] and not rec["pipe_closed_early"], "T14 EOF coinciding with Z is not an early close")
    check(rec["root_exited"] and signals(w) == [] and st == 0, "T14 normal completion, nothing signalled")


# T15: ps cannot be read: the group is still signalled (that needs no
# census), the record says the census FAILED, the lists are null -- never an
# empty list that would read as clean -- and cleanup is UNCERTAIN.
def t15():
    w = World(census_fails=True)
    rec, st = run(w, wall=1.0, grace=0.5)
    check(rec["census"].startswith("failed:"), "T15 census failure named")
    check(rec["left_in_group"] is None and rec["escaped_group"] is None and rec["escaped_session"] is None,
          "T15 observed lists are null, not empty")
    check(rec["cleanup"] == "uncertain", "T15 cleanup UNCERTAIN")
    check([c[2] for c in signals(w)] == [signal.SIGTERM, signal.SIGKILL] and all(c[1] == ROOT for c in signals(w)),
          "T15 the group was still signalled TERM then KILL (no census needed for that)")
    check(len(waits(w)) == 1, "T15 one wait after KILL")


# Portability: the module must parse as Python 3.9 (the Mac's /usr/bin/python3)
# and use only the ps spellings BSD ps and procps share.
def portability():
    import ast
    src = open(os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "lib", "supervise.py")).read()
    try:
        ast.parse(src, feature_version=(3, 9))
        check(True, "portability: parses with feature_version=(3, 9)")
    except SyntaxError as e:
        check(False, "portability: 3.9 parse failed: %s" % e)
    check("sys.version_info >= (3, 9)" in src, "portability: version guard present")
    for bad in ("--no-headers", " -e ", " -A ", "--ppid", "lstart", " -q ", "comm=", "sess="):
        check(bad not in src, "portability: no " + bad.strip() + " in ps spellings")
    check('"-ax -o pid=,ppid=,pgid=,stat="' in src and '"-o stat= -p "' in src,
          "portability: only the two shared ps forms")
    check("process_group=" not in src and "pidfd" not in src, "portability: no 3.10+ process APIs")
    imports = sorted(l.split()[1] for l in src.splitlines() if l.startswith("import "))
    check(imports == ["argparse", "json", "os", "select", "signal", "subprocess", "sys", "time"],
          "portability: stdlib imports are exactly the declared set (got %s)" % imports)
    import re
    start = src.index("subprocess.Popen(") + len("subprocess.Popen")
    depth, i = 0, start
    while True:  # the call's own balanced parenthesis, not the first ')'
        if src[i] == "(":
            depth += 1
        elif src[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    popen_call = src[start:i + 1]
    kw = sorted(re.findall(r"(\w+)=", popen_call))
    check(kw == ["close_fds", "pass_fds", "start_new_session", "stderr", "stdin", "stdout"],
          "portability: Popen kwargs are exactly the declared 3.9-present set (got %s)" % kw)
    check("syntax-checked against 3.9; runtime unverified" in src,
          "portability: the record states syntax-checked-only")


# The pure helpers.
def helpers():
    child = [(ROOT, 1, ROOT, "S"), (501, ROOT, ROOT, "S")]
    dead_root = [(ROOT, 1, ROOT, "Z")]
    check(supervise.members(child, ROOT, ROOT) == [501], "members: live non-root group members")
    check(supervise.members(dead_root, ROOT, ROOT) == [], "members: a zombie leader is not a member")
    census = [(ROOT, 1, ROOT, "Z"), (600, ROOT, 600, "S"), (601, 600, 600, "S"), (900, 1, 900, "S")]
    check(supervise.escaped(census, ROOT, ROOT) == [600, 601], "escaped: chain to root, other pgid")
    check(supervise.sigchld_ok(signal.SIG_DFL) and not supervise.sigchld_ok(signal.SIG_IGN),
          "sigchld_ok: only SIG_DFL")


# Grep invariants over the production module: the ownership argument is
# structural, so its shape is asserted, not trusted.
def invariants():
    src = open(os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "lib", "supervise.py")).read()
    check(src.count("os.waitpid(") == 1 and "os.waitpid(pid, os.WNOHANG)" in src,
          "invariant: exactly one os.waitpid site, and it is WNOHANG (never a blocking reap)")
    check("os.kill(" not in src, "invariant: no os.kill on a pid number")
    check("with subprocess.Popen" not in src, "invariant: the managed Popen is never a context manager")
    for bad in ("_proc.poll(", "_proc.wait(", "_proc.communicate(", "_proc.kill(", "_proc.terminate(",
                "_proc.send_signal("):
        check(bad not in src, "invariant: no " + bad + " on the managed object")
    check(src.count("_proc.returncode") == 1, "invariant: returncode on the managed object is written once, after the wait, never read")
    check(src.count("signal.signal(signal.SIGCHLD") == 1 and "signal.SIGCHLD, signal.SIG_DFL" in src,
          "invariant: SIGCHLD is only ever reset to SIG_DFL, never given a handler")
    check("start_new_session=True" in src, "invariant: the child starts its own session")
    check("os.getenv(" not in src and "os.environ" not in src and "HANABI_" not in src,
          "invariant: the supervisor reads no environment at all")
    check("os.system(" not in src and "shell=True" not in src and "/tmp/" not in src,
          "invariant: no shell, no predictable temp path")
    check(src.count("subprocess.run(") == 1 and '["ps"]' in src, "invariant: census is one subprocess.run of ps, no shell")
    start = src.index("subprocess.Popen(") + len("subprocess.Popen")
    depth, i = 0, start
    while True:
        if src[i] == "(":
            depth += 1
        elif src[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    popen_call = src[start:i + 1]
    check("env=" not in popen_call and "shell=" not in popen_call,
          "invariant: no env= dict and no shell on the managed Popen (env(1) in the argv builds the app's environment)")


if __name__ == "__main__":
    for t in (t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, helpers, portability, invariants):
        t()
    if failures:
        print("%d failure(s)" % failures)
        sys.exit(1)
    print("supervise unit ok")
