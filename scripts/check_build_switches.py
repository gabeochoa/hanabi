#!/usr/bin/env python3
"""scripts/check_build_switches.py -- the build switches stay what they are.

Knots kt-jcjp: Hanabi's compile-time defines were audited (2026-09-09) into
two kinds, and this keeps them that way.

  PRODUCT INVARIANTS (src/build_config.h, force-included into every app TU):
    AFTER_HOURS_UI_SINGLE_COLLECTION, AFTERHOURS_DEFAULT_TEXT_INSET,
    AFTERHOURS_MIN_FONT_SIZE, FMT_HEADER_ONLY.
    These are always on in the product. Nothing under src/ may BRANCH on one
    (#if/#ifdef/#ifndef/defined(...)): an "off" arm is dead code that no build
    compiles and no test reads. Outside build_config.h nothing under src/ may
    define one either. A test compiled without build_config.h may define
    FMT_HEADER_ONLY by hand -- only the files listed below.

  LINKAGE AND TEST VARIANTS: AFTER_HOURS_USE_METAL, HANABI_GPU_ACCOUNTING
    (the unit tests link without the Metal allocator and sokol), HANABI_ENABLE_TLS
    (the offline lane links without OpenSSL), AFTER_HOURS_ENABLE_E2E_TESTING
    (the scripted-UI build). Their branches are allowed only in the files that
    have them today; a new file that branches on one has to be added here, on
    purpose, with the reason it needs a second build.

--selftest plants each defect in a scratch copy and checks it is caught.
"""
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INVARIANTS = ["AFTER_HOURS_UI_SINGLE_COLLECTION", "AFTERHOURS_DEFAULT_TEXT_INSET",
              "AFTERHOURS_MIN_FONT_SIZE", "FMT_HEADER_ONLY"]
HAND_DEFINE_OK = {"tests/unit/test_input_pipeline.cpp", "tests/e2e/test_e2e.cpp"}
VARIANTS = {
    "AFTER_HOURS_USE_METAL": {"src/build_config.h", "src/ui/decode_to_fit.h",
                              "tests/vendor_probes/sokol_backend_smoke.mm"},
    "HANABI_GPU_ACCOUNTING": {"src/build_config.h", "src/util/gpu_mem.h", "src/util/soak.h"},
    "HANABI_ENABLE_TLS": {"src/api/agentcloud_auth.cpp", "src/api/agentcloud_client.cpp",
                          "src/api/http_client.cpp", "tests/e2e/test_real.cpp"},
    "AFTER_HOURS_ENABLE_E2E_TESTING": {
        "src/api/knots_runner.h", "src/ecs/bug_report_open.h", "src/ecs/capture_marker_system.h",
        "src/ecs/main_pane_system.h", "src/ecs/sidebar_system.h", "src/keys.h", "src/main.cpp",
        "src/native_extras.mm", "src/test_hooks.h", "src/ui/accessibility.h", "src/ui/audio_player.h",
        "src/ui/link_detect.h", "src/util/clipboard.h", "src/util/stress.h",
        "tests/unit/test_clipboard_isolation.cpp", "tests/unit/test_knots.cpp"},
}
EXTS = (".h", ".hpp", ".cpp", ".mm", ".m")
BRANCH = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$")
DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)")


def files(root):
    for top in ("src", "tests"):
        base = os.path.join(root, top)
        for dirpath, _, names in os.walk(base):
            for n in names:
                if n.endswith(EXTS):
                    p = os.path.join(dirpath, n)
                    yield os.path.relpath(p, root), p


def check(root):
    problems = []
    for rel, path in files(root):
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for ln, line in enumerate(text.splitlines(), 1):
            m = BRANCH.match(line)
            if m:
                cond = m.group(2)
                for name in INVARIANTS:
                    if re.search(r"\b%s\b" % name, cond) and rel.startswith("src/"):
                        problems.append(f"{rel}:{ln}: branches on the product invariant {name} (it is always on)")
                for name, allowed in VARIANTS.items():
                    if re.search(r"\b%s\b" % name, cond) and rel not in allowed:
                        problems.append(f"{rel}:{ln}: branches on {name}, a linkage/test variant not allowed here "
                                        f"(add the file to scripts/check_build_switches.py with the reason)")
            d = DEFINE.match(line)
            if d and d.group(1) in INVARIANTS and rel != "src/build_config.h":
                if rel.startswith("src/") or rel not in HAND_DEFINE_OK:
                    problems.append(f"{rel}:{ln}: defines the product invariant {d.group(1)} outside src/build_config.h")
    return problems


def selftest():
    import shutil
    plants = [
        ("src/ui/planted_a.h", "#ifdef FMT_HEADER_ONLY\n#endif\n", "branches on the product invariant"),
        ("src/ui/planted_b.h", "#if defined(HANABI_GPU_ACCOUNTING)\n#endif\n", "linkage/test variant"),
        ("src/ui/planted_c.h", "#define AFTERHOURS_MIN_FONT_SIZE 9\n", "defines the product invariant"),
        ("tests/unit/planted_d.cpp", "#define FMT_HEADER_ONLY\n", "defines the product invariant"),
    ]
    caught = 0
    for rel, body, want in plants:
        with tempfile.TemporaryDirectory() as tmp:
            for top in ("src", "tests"):
                os.makedirs(os.path.join(tmp, top), exist_ok=True)
            p = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").write(body)
            if any(want in x for x in check(tmp)):
                caught += 1
                print(f"  selftest: caught {rel}")
            else:
                print(f"  selftest: MISSED {rel}")
    print(f"selftest: {caught}/{len(plants)} planted defects caught")
    return caught == len(plants)


def main():
    if "--selftest" in sys.argv:
        return 0 if selftest() else 1
    problems = check(ROOT)
    if problems:
        print("check_build_switches: FAIL")
        for p in problems:
            print("  " + p)
        return 1
    print("check_build_switches: product invariants unconditional; linkage/test variants only where allowed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
