#!/usr/bin/env python3
"""build.zig names its sources explicitly. This holds the lists to the tree:

  * every .cpp/.mm under src/ is in exactly one of the app's three groups,
    and nothing in a group is missing from the tree;
  * every tests/unit/*.cpp|.mm and tests/e2e/*.cpp is the first source of one
    TestSpec row, and every row's sources exist;
  * every name in run_by_default is a TestSpec.

A new source file that is not added to build.zig fails here rather than
linking without it (a header-only symbol would go missing at link time; a
test would silently never run). Part of source-checks.
"""
from __future__ import annotations
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / "build.zig").read_text()

def block(name: str) -> str:
    m = re.search(rf"const {name} = \[_\]\[\]const u8\{{(.*?)\}};|const {name} = \[_\]TestSpec\{{(.*?)\n\}};", text, re.S)
    if not m:
        raise SystemExit(f"check_build_graph: cannot find `{name}` in build.zig")
    return m.group(1) if m.group(1) is not None else m.group(2)

def strings(s: str) -> list[str]:
    return re.findall(r'"([^"]+)"', s)

failures: list[str] = []

app = strings(block("app_sources")) + strings(block("app_objc_sources")) + strings(block("app_arc_sources")) + strings(block("app_stamp_source"))
tree = sorted(str(p.relative_to(ROOT)) for p in ROOT.glob("src/**/*") if p.suffix in (".cpp", ".mm"))
for f in tree:
    if f not in app:
        failures.append(f"{f}: in the tree, not in build.zig's app source lists")
for f in app:
    if not (ROOT / f).exists():
        failures.append(f"{f}: in build.zig, not in the tree")
    if app.count(f) > 1:
        failures.append(f"{f}: listed twice in build.zig")

rows = re.findall(r'\.\{ \.name = "([a-z0-9_]+)", \.kind = \.(\w+), \.arc = (true|false), \.srcs = &\.\{ ([^}]*) \}, \.frameworks = &\.\{ ([^}]*) \} \}', block("tests"))
by_name = {}
for name, kind, arc, srcs, fws in rows:
    srcs = strings(srcs)
    by_name[name] = srcs
    if not srcs:
        failures.append(f"{name}: no sources")
        continue
    if Path(srcs[0]).stem != name:
        failures.append(f"{name}: first source is {srcs[0]}, expected tests/.../{name}.cpp")
    for s in srcs:
        if not (ROOT / s).exists():
            failures.append(f"{name}: source {s} does not exist")
test_files = sorted(str(p.relative_to(ROOT)) for p in list(ROOT.glob("tests/unit/*.cpp")) + list(ROOT.glob("tests/unit/*.mm")) + list(ROOT.glob("tests/e2e/*.cpp")))
firsts = {srcs[0] for srcs in by_name.values() if srcs}
for f in test_files:
    if f not in firsts:
        failures.append(f"{f}: a test source with no TestSpec row in build.zig")

for name in strings(block("run_by_default")):
    if name not in by_name:
        failures.append(f"run_by_default names {name}, which has no TestSpec row")

if failures:
    print("check_build_graph: build.zig and the tree disagree:")
    for f in failures:
        print("  " + f)
    sys.exit(1)
print(f"check_build_graph: {len(app)} app sources, {len(rows)} test rows, {len(strings(block('run_by_default')))} run by default; all resolve")
