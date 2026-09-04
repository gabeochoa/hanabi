#!/usr/bin/env python3

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor" / "afterhours"
PROBES = ROOT / "tests" / "vendor_probes"
BASE = "8d421a6cf787eb83272e4c449c7f646b7e4f4ff8"
PIN = "8d421a6cf787eb83272e4c449c7f646b7e4f4ff8"
CXX = shlex.split(os.environ.get("CXX", "clang++"))
PATCHES = {
    "351-report-font-atlas-exhaustion.patch": "atlas",
    "210-reject-unsamplable-textures.patch": "sampler",
    "265-focus-ring-contrast-toggle.patch": "focus",
    "255-word-editing-capability.patch": "word",
}


def run(
    args: list[str], *, cwd: Optional[Path] = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def require_ok(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0:
        return
    sys.stderr.write(f"{label} failed\n{result.stdout}{result.stderr}")
    raise SystemExit(1)


def require_red(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        return
    sys.stderr.write(f"{label} was unexpectedly green on the pinned base\n")
    raise SystemExit(1)


def vendor_revision() -> subprocess.CompletedProcess[str]:
    if (VENDOR / ".hg").exists():
        return run(["sl", "log", "-r", ".", "-T", "{node}\\n"], cwd=VENDOR)
    return run(["git", "rev-parse", "HEAD"], cwd=VENDOR)


def export_tree(destination: Path, revision: str) -> None:
    destination.mkdir(parents=True)
    archive = subprocess.Popen(
        ["git", "-C", str(VENDOR), "archive", revision], stdout=subprocess.PIPE
    )
    assert archive.stdout is not None
    extracted = subprocess.run(["tar", "-x", "-C", str(destination)], stdin=archive.stdout)
    archive.stdout.close()
    if extracted.returncode != 0 or archive.wait() != 0:
        raise SystemExit(f"could not export vendor tree {revision}")


def export_base(destination: Path) -> None:
    if (VENDOR / ".hg").exists():
        shutil.copytree(
            VENDOR,
            destination,
            ignore=shutil.ignore_patterns(".hg", ".git"),
        )
        return
    destination.mkdir(parents=True)
    archive = subprocess.Popen(
        ["git", "-C", str(VENDOR), "archive", BASE], stdout=subprocess.PIPE
    )
    assert archive.stdout is not None
    extracted = subprocess.run(["tar", "-x", "-C", str(destination)], stdin=archive.stdout)
    archive.stdout.close()
    if extracted.returncode != 0 or archive.wait() != 0:
        raise SystemExit("could not export pinned vendor base")


def compile_probe(tree: Path, source: Path, output: Path, syntax_only: bool = False) -> subprocess.CompletedProcess[str]:
    args = CXX + ["-std=c++23", "-O0", "-w"]
    if syntax_only:
        args += ["-fsyntax-only", "-x", "objective-c++"]
    args += [
        f"-I{tree.parent}",
        f"-I{tree / 'vendor'}",
        f"-I{ROOT / 'vendor'}",
        str(source),
    ]
    if not syntax_only:
        args += ["-o", str(output)]
    return run(args)


def compile_and_run(tree: Path, source: str, output: Path) -> subprocess.CompletedProcess[str]:
    built = compile_probe(tree, PROBES / source, output)
    if built.returncode != 0:
        return built
    return run([str(output)])


def check_contract(binary: Path, kind: str, tree: Path) -> subprocess.CompletedProcess[str]:
    filename = "backend.h" if kind == "atlas" else "drawing_helpers.h"
    return run(
        [
            str(binary),
            kind,
            str(tree / "src" / "backends" / "sokol" / filename),
        ]
    )


def verify_patch(temp: Path, base_tree: Path, pin_tree: Path, contract: Path,
                 patch_name: str, kind: str) -> None:
    patch = ROOT / "vendor_patches" / patch_name
    if PIN not in patch.read_text():
        raise SystemExit(f"{patch_name}: not generated against the pin {PIN}")

    patched_tree = temp / patch.stem / "afterhours"
    shutil.copytree(pin_tree, patched_tree)
    require_ok(run(["git", "init", "-q"], cwd=patched_tree), f"{patch_name}: git init")
    require_ok(
        run(["git", "apply", "--check", str(patch)], cwd=patched_tree),
        f"{patch_name}: does not apply to the pinned tree",
    )
    require_ok(run(["git", "apply", str(patch)], cwd=patched_tree), f"{patch_name}: apply")
    base_tree = pin_tree

    if kind in {"atlas", "sampler"}:
        require_red(check_contract(contract, kind, base_tree), f"{patch_name}: red contract")
        require_ok(check_contract(contract, kind, patched_tree), f"{patch_name}: green contract")
        require_ok(
            compile_probe(
                patched_tree,
                PROBES / "sokol_backend_smoke.mm",
                temp / f"{kind}-unused",
                syntax_only=True,
            ),
            f"{patch_name}: pinned Sokol compile",
        )
    elif kind == "e2e":
        require_red(
            compile_and_run(base_tree, "e2e_diagnostics_probe.cpp", temp / "e2e-before"),
            f"{patch_name}: red probe",
        )
        require_ok(
            compile_and_run(patched_tree, "e2e_diagnostics_probe.cpp", temp / "e2e-after"),
            f"{patch_name}: green probe",
        )
    elif kind == "focus":
        require_red(
            compile_and_run(base_tree, "focus_ring_contrast_probe.cpp", temp / "focus-before"),
            f"{patch_name}: red probe",
        )
        require_ok(
            compile_and_run(patched_tree, "focus_ring_contrast_probe.cpp", temp / "focus-after"),
            f"{patch_name}: green probe",
        )
    elif kind == "word":
        require_red(
            compile_probe(
                base_tree,
                PROBES / "word_editing_capability_probe.cpp",
                temp / "word-before",
            ),
            f"{patch_name}: red compile",
        )
        require_ok(
            compile_and_run(
                patched_tree,
                "word_editing_capability_probe.cpp",
                temp / "word-after",
            ),
            f"{patch_name}: green probe",
        )

    print(f"PASS {patch_name}: red before, green after -- at the pin")


def main() -> int:
    actual = vendor_revision()
    require_ok(actual, "read vendor revision")
    if actual.stdout.strip() != PIN:
        sys.stderr.write(f"vendor revision is {actual.stdout.strip()}, expected {PIN}\n")
        return 1

    with tempfile.TemporaryDirectory(prefix="hanabi-vendor-patches-") as raw_temp:
        temp = Path(raw_temp)
        base_tree = temp / "base" / "afterhours"
        export_base(base_tree)
        pin_tree = temp / "pin" / "afterhours"
        export_tree(pin_tree, PIN)
        contract = temp / "source-contract-probe"
        require_ok(
            run(CXX + ["-std=c++23", "-O0", "-w", str(PROBES / "source_contract_probe.cpp"), "-o", str(contract)]),
            "compile source contract probe",
        )
        for patch_name, kind in PATCHES.items():
            verify_patch(temp, base_tree, pin_tree, contract, patch_name, kind)

    print(f"PASS all {len(PATCHES)} vendor patches against the pinned "
          f"tree {PIN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
