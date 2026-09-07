#!/usr/bin/env python3
"""Every setting must be read by something that is not the settings sheet.

A switch that persists a value nothing consumes looks finished and does
nothing: the reader flips it, the app does not change, and no test fails
because the only code that touches the value is the sheet that wrote it. This
project has shipped that bug more than once -- a notifications master switch
the post path ignored, a per-chord on/off the key matcher ignored -- so the
rule is a gate rather than a habit.

The rule: for every `get_*` on Settings, some file OUTSIDE settings.{h,cpp} and
the settings sheet must call it. Tests do not count; a test reading a value
proves the store works, not that the app obeys it.

EXEMPT lists the accessors where the sheet legitimately IS the reader, each
with the reason. Adding a name here is a claim that the setting has no
behaviour outside the sheet -- if that is not true, wire the reader instead.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The sheet and the store itself: a call from here is the write side, not a
# reader.
SHEET = {
    "src/settings.h",
    "src/settings.cpp",
    "src/ecs/settings_system.h",
}

EXEMPT = {
    # The sheet's own place in itself.
    "get_settings_pane": "which pane the sheet reopens on",
    # Composed accessors: the reader outside calls the composite, not these.
    "get_global_enabled": "read through get_global_requests (main.cpp)",
    "get_global_shortcut": "read through get_global_requests (main.cpp)",
    "get_shortcuts": "read through get_shortcut / validate",
    # Where an export lands is chosen and used inside the export row itself.
    "get_export_dir": "the export row is both chooser and consumer",
    # Sidebar state that predates this gate and is read through its own model.
    "get_row_order": "sidebar ordering, read via the sidebar model",
    "get_starred": "sidebar starring, read via the sidebar model",
}


def getters():
    header = open(os.path.join(ROOT, "src/settings.h"), encoding="utf-8").read()
    return sorted(set(re.findall(r"\b(get_[a-z0-9_]+)\s*\(", header)))


def readers(name):
    out = subprocess.run(
        ["grep", "-rl", name + "(", "src", "scripts"],
        cwd=ROOT, capture_output=True, text=True).stdout.split()
    return [f for f in out if f not in SHEET]


def main():
    dead = []
    checked = 0
    for name in getters():
        if name in EXEMPT:
            continue
        checked += 1
        if not readers(name):
            dead.append(name)

    if dead:
        print("check_settings_readers: FAIL", file=sys.stderr)
        for name in dead:
            print(f"  Settings::{name}() has no reader outside the settings "
                  f"sheet -- the control that writes it does nothing",
                  file=sys.stderr)
        print("  Wire a reader, or add the name to EXEMPT with the reason.",
              file=sys.stderr)
        return 1

    print(f"check_settings_readers: {checked} settings, "
          f"{len(EXEMPT)} exempt, every one has a reader")
    return 0


if __name__ == "__main__":
    sys.exit(main())
