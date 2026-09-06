#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
LOCAL_TEST = ROOT / "scripts/test_agentcloud_local.sh"
BINARY = ROOT / "output/tests/test_agentcloud_local"


def run_fixture(mutant: bool) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if mutant:
        env["HANABI_ATTACHMENT_ROUTE_MUTANT"] = "early-success"
    else:
        env.pop("HANABI_ATTACHMENT_ROUTE_MUTANT", None)
    return subprocess.run(
        ["bash", str(LOCAL_TEST)],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=90,
    )


def fail(label: str, result: subprocess.CompletedProcess[str]) -> None:
    print(f"attachment-route-gate: FAIL: {label}", file=sys.stderr)
    print(result.stdout[-4000:], file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.parse_args()
    built = subprocess.run(
        ["make", "-s", str(BINARY.relative_to(ROOT))],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=180,
    )
    if built.returncode != 0:
        fail("could not build the local protocol control", built)

    control = run_fixture(mutant=False)
    if control.returncode != 0:
        fail("the real attachment route control failed", control)

    bypass = run_fixture(mutant=True)
    if bypass.returncode == 0:
        fail("an early fake-success bypass escaped detection", bypass)
    if "attachment HTTP route was never called" not in bypass.stdout:
        fail("the planted bypass failed for the wrong reason", bypass)

    reverted = run_fixture(mutant=False)
    if reverted.returncode != 0:
        fail("the control did not recover after removing the mutant", reverted)

    print(
        "attachment-route-gate: PASS "
        "(real route reached; early-success mutant rejected; revert passed)"
    )


if __name__ == "__main__":
    main()
