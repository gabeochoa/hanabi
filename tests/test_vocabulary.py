import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_vocabulary", ROOT / "scripts/check_vocabulary.py")
cv = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cv)

BANNED = cv.FORBIDDEN[0]
BANNED_TWO = cv.FORBIDDEN[1]

GIT_ENV = {
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_SYSTEM": "/dev/null",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "fixture",
    "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
    "GIT_COMMITTER_NAME": "fixture",
    "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
    "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
    "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z",
    "HOME": "/nonexistent",
    "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
}


def git(repo, *args):
    done = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgsign=false",
         "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main", *args],
        cwd=repo, env=GIT_ENV, capture_output=True, text=True)
    if done.returncode != 0:
        raise AssertionError(f"fixture git {' '.join(args)} failed: {done.stderr}")
    return done.stdout


def make_repo(tmp):
    repo = Path(tmp) / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    (repo / "base.txt").write_text("a quiet line\n", encoding="utf-8")
    git(repo, "add", "base.txt")
    git(repo, "commit", "-q", "-m", "base")
    git(repo, "update-ref", "refs/remotes/origin/main", "HEAD")
    return repo


def run_delta(root, base_ref="origin/main"):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = cv.scan_delta(root=str(root), base_ref=base_ref)
    return rc, out.getvalue(), err.getvalue()


def run_tree(root):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = cv.scan_tree(root=str(root))
    return rc, out.getvalue(), err.getvalue()


class DeltaModeFailsClosedWithoutGit(unittest.TestCase):
    def test_a_directory_that_is_not_a_repository_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            plain = Path(tmp) / "plain"
            plain.mkdir()
            (plain / "x.md").write_text(f"{BANNED}\n", encoding="utf-8")
            rc, out, err = run_delta(plain)
        self.assertEqual(rc, 2)
        self.assertIn("NOT SCANNED", err)
        self.assertIn("rev-parse", err)
        self.assertIn("--tree", err)
        self.assertEqual(out, "")

    def test_an_absent_base_ref_exits_2_and_never_falls_back_to_a_literal(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            git(repo, "update-ref", "-d", "refs/remotes/origin/main")
            (repo / "new.md").write_text(f"{BANNED}\n", encoding="utf-8")
            git(repo, "add", "new.md")
            git(repo, "commit", "-q", "-m", "adds a banned word")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("origin/main", err)
        self.assertNotIn("no prohibited", out)

    def test_a_malformed_base_ref_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            rc, out, err = run_delta(repo, base_ref="not a ref")
        self.assertEqual(rc, 2)
        self.assertIn("NOT SCANNED", err)

    def test_a_base_with_no_common_ancestor_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            git(repo, "checkout", "-q", "--orphan", "island")
            git(repo, "rm", "-rfq", ".")
            (repo / "other.md").write_text("unrelated\n", encoding="utf-8")
            git(repo, "add", "other.md")
            git(repo, "commit", "-q", "-m", "island")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("merge-base", err)

    def test_an_unborn_head_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            git(repo, "init", "-q")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("HEAD", err)

    def test_a_failing_diff_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            real = subprocess.run

            def failing_diff(cmd, **kw):
                if cmd[:2] == ["git", "diff"]:
                    return subprocess.CompletedProcess(cmd, 128, "", "fatal: injected diff failure\n")
                return real(cmd, **kw)

            with mock.patch.object(cv.subprocess, "run", side_effect=failing_diff):
                rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("injected diff failure", err)

    def test_a_failing_untracked_enumeration_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            real = subprocess.run

            def failing_ls(cmd, **kw):
                if cmd[:2] == ["git", "ls-files"]:
                    return subprocess.CompletedProcess(cmd, 1, "", "fatal: injected ls-files failure\n")
                return real(cmd, **kw)

            with mock.patch.object(cv.subprocess, "run", side_effect=failing_ls):
                rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("injected ls-files failure", err)

    def test_git_missing_from_path_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            with mock.patch.object(cv.subprocess, "run", side_effect=FileNotFoundError(2, "git")):
                rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("cannot run git", err)

    def test_a_non_work_tree_answer_exits_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            real = subprocess.run

            def bare_answer(cmd, **kw):
                if cmd[:3] == ["git", "rev-parse", "--is-inside-work-tree"]:
                    return subprocess.CompletedProcess(cmd, 0, "false\n", "")
                return real(cmd, **kw)

            with mock.patch.object(cv.subprocess, "run", side_effect=bare_answer):
                rc, out, err = run_delta(repo)
        self.assertEqual(rc, 2)
        self.assertIn("not a work tree", err)


class DeltaModeVerdicts(unittest.TestCase):
    def test_a_banned_added_line_exits_1_and_names_the_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "notes.md").write_text(f"about {BANNED} here\n", encoding="utf-8")
            git(repo, "add", "notes.md")
            git(repo, "commit", "-q", "-m", "adds")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL", err)
        self.assertIn("notes.md", err)
        self.assertIn("1 added occurrence(s) in 1 added line(s)", err)
        self.assertNotIn(BANNED, err)

    def test_a_clean_added_line_exits_0_with_the_added_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "notes.md").write_text("metadata and metal are fine\nsecond\n", encoding="utf-8")
            git(repo, "add", "notes.md")
            git(repo, "commit", "-q", "-m", "adds")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 0)
        self.assertIn("no prohibited project names added (2 added line(s) scanned)", out)
        self.assertEqual(err, "")

    def test_a_legitimate_empty_delta_exits_0_and_says_zero_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 0)
        self.assertIn("(0 added line(s) scanned)", out)

    def test_a_pre_existing_occurrence_at_the_base_is_not_a_finding(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "old.md").write_text(f"{BANNED_TWO} was here first\n", encoding="utf-8")
            git(repo, "add", "old.md")
            git(repo, "commit", "-q", "-m", "history")
            git(repo, "update-ref", "refs/remotes/origin/main", "HEAD")
            (repo / "new.md").write_text("nothing to see\n", encoding="utf-8")
            git(repo, "add", "new.md")
            git(repo, "commit", "-q", "-m", "clean")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 0)

    def test_an_untracked_file_with_prohibited_text_exits_1(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "scratch.txt").write_text(f"line one\n{BANNED_TWO} on two\n", encoding="utf-8")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 1)
        self.assertIn("scratch.txt:2", err)

    def test_an_untracked_file_with_an_unlisted_suffix_is_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "blob.bin").write_text(f"{BANNED}\n", encoding="utf-8")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 0)

    def test_an_untracked_file_under_a_skipped_directory_is_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "vendor").mkdir()
            (repo / "vendor" / "third.h").write_text(f"{BANNED}\n", encoding="utf-8")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 0)

    def test_uncommitted_tracked_edits_count_as_added(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "base.txt").write_text(f"a quiet line\n{BANNED}\n", encoding="utf-8")
            rc, out, err = run_delta(repo)
        self.assertEqual(rc, 1)
        self.assertIn("base.txt", err)


class TreeModeNeedsNoGit(unittest.TestCase):
    def test_tree_mode_scans_a_plain_directory_and_exits_0(self):
        with tempfile.TemporaryDirectory() as tmp:
            plain = Path(tmp) / "plain"
            plain.mkdir()
            (plain / "a.md").write_text(f"{BANNED}\n", encoding="utf-8")
            (plain / "b.py").write_text("clean\n", encoding="utf-8")
            (plain / "c.bin").write_text(f"{BANNED}\n", encoding="utf-8")
            rc, out, err = run_tree(plain)
        self.assertEqual(rc, 0)
        self.assertIn("2 files scanned, 1 pre-existing occurrence(s)", out)
        self.assertEqual(err, "")

    def test_main_hands_the_patched_root_to_both_scanners(self):
        seen = []
        with mock.patch.object(cv, "ROOT", "/spy/root"), \
             mock.patch.object(cv, "scan_tree", side_effect=lambda root: seen.append(("tree", root)) or 0), \
             mock.patch.object(cv, "scan_delta", side_effect=lambda root: seen.append(("delta", root)) or 0):
            rc_tree = cv.main(["--tree"])
            rc_delta = cv.main([])
        self.assertEqual((rc_tree, rc_delta), (0, 0))
        self.assertEqual(seen, [("tree", "/spy/root"), ("delta", "/spy/root")])

    def test_main_routes_tree_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            plain = Path(tmp) / "plain"
            plain.mkdir()
            with mock.patch.object(cv, "ROOT", str(plain)):
                out, err = io.StringIO(), io.StringIO()
                with redirect_stdout(out), redirect_stderr(err):
                    rc_tree = cv.main(["--tree"])
                    rc_delta = cv.main([])
        self.assertEqual(rc_tree, 0)
        self.assertEqual(rc_delta, 2)


class TheScriptAsAProcess(unittest.TestCase):
    def test_exit_codes_through_the_interpreter(self):
        script = ROOT / "scripts/check_vocabulary.py"
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(tmp)
            (repo / "n.md").write_text(f"{BANNED}\n", encoding="utf-8")
            git(repo, "add", "n.md")
            git(repo, "commit", "-q", "-m", "n")
            src = (ROOT / "scripts/check_vocabulary.py").read_text(encoding="utf-8")
            (repo / "scripts").mkdir()
            (repo / "scripts" / "check_vocabulary.py").write_text(src, encoding="utf-8")
            env = dict(GIT_ENV)
            red = subprocess.run([sys.executable, str(repo / "scripts/check_vocabulary.py")],
                                 cwd=repo, env=env, capture_output=True, text=True)
            self.assertEqual(red.returncode, 1, red.stderr)
            plain = Path(tmp) / "plain"
            plain.mkdir()
            (plain / "scripts").mkdir()
            (plain / "scripts" / "check_vocabulary.py").write_text(src, encoding="utf-8")
            closed = subprocess.run([sys.executable, str(plain / "scripts/check_vocabulary.py")],
                                    cwd=plain, env=env, capture_output=True, text=True)
            self.assertEqual(closed.returncode, 2, closed.stderr)
            tree = subprocess.run([sys.executable, str(plain / "scripts/check_vocabulary.py"), "--tree"],
                                  cwd=plain, env=env, capture_output=True, text=True)
            self.assertEqual(tree.returncode, 0, tree.stderr)
        self.assertTrue(script.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
