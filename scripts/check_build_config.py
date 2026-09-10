#!/usr/bin/env python3
"""scripts/check_build_config.py -- src/build_config.h is the only place a product invariant is defined, and every conditional build switch is declared here with both arms."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_CONFIG = "src/build_config.h"
MAKEFILE = "makefile"

LINKAGE_SWITCHES = {
    "AFTER_HOURS_USE_METAL": (
        "makefile GPU_BACKEND_DEFINES: the app and hanabi_uitest link sokol/Metal",
        "the headless unit/e2e/perf binaries; test_downscale compiles "
        "decode_to_fit.h's non-Metal arm",
    ),
    "HANABI_GPU_ACCOUNTING": (
        "makefile GPU_BACKEND_DEFINES: the app and hanabi_uitest link src/gpu_mem.mm",
        "the unit tests, which report device bytes as not measured (test_gpu_mem)",
    ),
    "HANABI_ENABLE_TLS": (
        "makefile when HANABI_TLS=1: OpenSSL is linked",
        "TEST_CXXFLAGS (the offline test lane) and HANABI_TLS=0 portability builds",
    ),
    "AFTER_HOURS_ENABLE_E2E_TESTING": (
        "makefile UITEST_CXXFLAGS: hanabi_uitest",
        "the shipping app",
    ),
}

SCAN_ROOTS = ("src", "tests", "tools")
SKIP_DIRS = ("tests/vendor_probes/",)
SOURCE_SUFFIXES = {".h", ".cpp", ".mm", ".m", ".c"}

FLAG_FAMILIES = ("CXXFLAGS", "TEST_CXXFLAGS", "PERF_CXXFLAGS")
FORCE_INCLUDE = "-include src/build_config.h"

SWITCH_NAME = re.compile(r"\b((?:HANABI|AFTER_HOURS|AFTERHOURS|FMT)_[A-Z0-9_]+)\b")
DEFINE = re.compile(r"^\s*#\s*(?:define|undef)\s+([A-Za-z_][A-Za-z0-9_]*)")
CONDITIONAL = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
MAKE_DEFINE = re.compile(r"-D([A-Za-z_][A-Za-z0-9_]*)")


def invariants_in(build_config_text: str) -> set[str]:
    return {
        m.group(1)
        for line in build_config_text.splitlines()
        if (m := DEFINE.match(line))
    }


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0]


def check_sources(files: dict[str, str], invariants: set[str]) -> list[str]:
    problems: list[str] = []
    for rel, text in sorted(files.items()):
        if rel == BUILD_CONFIG or rel.startswith(SKIP_DIRS):
            continue
        for number, raw in enumerate(text.splitlines(), start=1):
            line = strip_line_comment(raw)
            if m := DEFINE.match(line):
                name = m.group(1)
                if name in invariants:
                    problems.append(
                        f"{rel}:{number}: defines {name}, which is a product "
                        f"invariant owned by {BUILD_CONFIG}"
                    )
                elif name in LINKAGE_SWITCHES:
                    problems.append(
                        f"{rel}:{number}: defines {name} by hand; the linkage "
                        f"switch comes from the makefile: {LINKAGE_SWITCHES[name][0]}"
                    )
                continue
            if m := CONDITIONAL.match(line):
                for name in SWITCH_NAME.findall(m.group(1)):
                    if name in invariants:
                        problems.append(
                            f"{rel}:{number}: tests {name}, which every binary "
                            f"defines through {BUILD_CONFIG}; one arm is dead"
                        )
                    elif name not in LINKAGE_SWITCHES:
                        problems.append(
                            f"{rel}:{number}: tests {name}, an undeclared build "
                            f"switch; declare it in scripts/check_build_config.py "
                            f"with both arms named, or make it unconditional"
                        )
    return problems


def check_makefile(text: str, invariants: set[str]) -> list[str]:
    problems: list[str] = []
    for family in FLAG_FAMILIES:
        assignment = re.search(
            rf"^{family}\s*:=(?P<body>(?:.*\\\n)*.*)$", text, re.MULTILINE
        )
        if assignment is None:
            problems.append(f"{MAKEFILE}: no `{family} :=` assignment found")
        elif FORCE_INCLUDE not in assignment.group("body"):
            problems.append(
                f"{MAKEFILE}: {family} does not carry `{FORCE_INCLUDE}`, so "
                f"its binaries compile against a different product config"
            )
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.split("#", 1)[0]
        for name in MAKE_DEFINE.findall(line):
            if name in invariants:
                problems.append(
                    f"{MAKEFILE}:{number}: -D{name} duplicates {BUILD_CONFIG}"
                )
            elif SWITCH_NAME.fullmatch(name) and name not in LINKAGE_SWITCHES:
                problems.append(
                    f"{MAKEFILE}:{number}: -D{name} is an undeclared build "
                    f"switch; declare it in scripts/check_build_config.py"
                )
    for name, (on_arm, _) in LINKAGE_SWITCHES.items():
        if on_arm.startswith("makefile") and f"-D{name}" not in text:
            problems.append(
                f"{MAKEFILE}: never passes -D{name}, but "
                f"scripts/check_build_config.py says its ON arm is: {on_arm}"
            )
    return problems


def check(files: dict[str, str], makefile_text: str) -> list[str]:
    invariants = invariants_in(files.get(BUILD_CONFIG, ""))
    if not invariants:
        return [f"{BUILD_CONFIG}: defines nothing, or is missing"]
    return check_sources(files, invariants) + check_makefile(makefile_text, invariants)


def source_files() -> dict[str, str]:
    out: dict[str, str] = {}
    for root in SCAN_ROOTS:
        for path in sorted((ROOT / root).rglob("*")):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                out[path.relative_to(ROOT).as_posix()] = path.read_text(
                    errors="replace"
                )
    return out


GOOD_CONFIG = "#pragma once\n#define FMT_HEADER_ONLY\n#define AFTER_HOURS_UI_SINGLE_COLLECTION\n"
GOOD_MAKEFILE = (
    "GPU_BACKEND_DEFINES := -DAFTER_HOURS_USE_METAL -DHANABI_GPU_ACCOUNTING\n"
    "CXXFLAGS := -std=c++23 \\\n    -include src/build_config.h $(GPU_BACKEND_DEFINES)\n"
    "    CXXFLAGS += -DHANABI_ENABLE_TLS\n"
    "TEST_CXXFLAGS := -std=c++23 -include src/build_config.h\n"
    "PERF_CXXFLAGS := -std=c++23 -include src/build_config.h\n"
    "UITEST_CXXFLAGS := $(CXXFLAGS) -DAFTER_HOURS_ENABLE_E2E_TESTING\n"
)


def selftest() -> int:
    base = {BUILD_CONFIG: GOOD_CONFIG}
    cases = [
        ("clean tree", dict(base, **{"src/a.h": "int x;\n"}), GOOD_MAKEFILE, True),
        (
            "stray define in a test",
            dict(base, **{"tests/unit/t.cpp": "#define FMT_HEADER_ONLY\n"}),
            GOOD_MAKEFILE,
            False,
        ),
        (
            "conditional on an invariant",
            dict(base, **{"src/a.h": "#ifdef AFTER_HOURS_UI_SINGLE_COLLECTION\n#endif\n"}),
            GOOD_MAKEFILE,
            False,
        ),
        (
            "silent fallback for a generated macro",
            dict(base, **{"src/a.h": "#ifndef HANABI_BUILD_STAMP\n#define HANABI_BUILD_STAMP \"?\"\n#endif\n"}),
            GOOD_MAKEFILE,
            False,
        ),
        (
            "declared linkage switch",
            dict(base, **{"src/a.cpp": "#ifdef HANABI_ENABLE_TLS\n#endif\n"}),
            GOOD_MAKEFILE,
            True,
        ),
        (
            "linkage switch defined by hand",
            dict(base, **{"src/a.mm": "#define AFTER_HOURS_USE_METAL\n"}),
            GOOD_MAKEFILE,
            False,
        ),
        (
            "vendor probe may define its own",
            dict(base, **{"tests/vendor_probes/p.mm": "#define AFTER_HOURS_USE_METAL\n"}),
            GOOD_MAKEFILE,
            True,
        ),
        (
            "invariant mentioned in a comment",
            dict(base, **{"src/a.h": "int x;  // FMT_HEADER_ONLY is set\n"}),
            GOOD_MAKEFILE,
            True,
        ),
        (
            "tests without the product config",
            dict(base, **{"src/a.h": "int x;\n"}),
            GOOD_MAKEFILE.replace("TEST_CXXFLAGS := -std=c++23 -include src/build_config.h", "TEST_CXXFLAGS := -std=c++23"),
            False,
        ),
        (
            "makefile duplicates an invariant",
            dict(base, **{"src/a.h": "int x;\n"}),
            GOOD_MAKEFILE + "CXXFLAGS += -DFMT_HEADER_ONLY\n",
            False,
        ),
        (
            "makefile drops a declared ON arm",
            dict(base, **{"src/a.h": "int x;\n"}),
            GOOD_MAKEFILE.replace(" -DHANABI_GPU_ACCOUNTING", ""),
            False,
        ),
    ]
    failed = [
        name
        for name, files, makefile, should_pass in cases
        if (not check(files, makefile)) != should_pass
    ]
    if failed:
        print("check_build_config selftest: FAIL: " + ", ".join(failed))
        return 1
    print(f"check_build_config selftest: {len(cases)} cases passed")
    return 0


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest()
    problems = check(source_files(), (ROOT / MAKEFILE).read_text())
    if problems:
        print("check_build_config: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(
        f"check_build_config: {BUILD_CONFIG} is the only definer of its "
        f"macros, and every conditional switch is declared with both arms"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
