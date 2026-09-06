#!/usr/bin/env python3
"""scripts/check_theme_config.py — a theme write that survives the frame.

afterhours' ThemeDefaults holds two Themes (upstream b6af466):

    app_default -- what the app configured. Persists.
    theme       -- what is in effect THIS FRAME. ThemeDefaults::begin_frame()
                   restores it from app_default at the top of every frame.

Everything that resolves a colour or a layout metric reads `theme`, so a write
to `theme` alone is a per-FRAME override that vanishes at the next
begin_frame. For a screen that is the point. For app configuration it is a
silent revert, and it is invisible in review because the write looks correct.

Hanabi shipped exactly that bug on the fc4d625..9ff9079 pin bump: font_sizing
set in preload was gone by frame one (every FontSize::Large label fell back to
the library's tier), and theme.ui_scale reverted to 1.0, which made
HANABI_UI_SCALE=2 lay the whole app out at half size --
tests/ui/ui_scale_is_a_zoom_not_a_bigger_canvas measured sidebar w=280 where
560 was required.

So: every write to `ThemeDefaults::get().theme.<field>` must be followed by
hanabi::ui::publish_app_theme() in the same function. afterhours' own setters
(set_theme, set_theme_color, set_click_activation_mode, set_highlight_mode)
already write both slots and are not flagged.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# A write through the singleton to a field of the FRAME theme.
WRITE = re.compile(
    r"ThemeDefaults::get\(\)\s*\.\s*theme\s*\.\s*(?P<field>\w+)\s*="
)
# The same, spelled through a local reference: `auto& theme = ...theme;`
ALIAS_BIND = re.compile(
    r"auto&\s*(?P<name>\w+)\s*=\s*[\w:]*ThemeDefaults::get\(\)\s*\.\s*theme\s*;"
)
# ...and through a reference to one of the theme's SUB-STRUCTS:
#   auto& sizing = ...theme.font_sizing;
#   sizing.xl = 17.f;                      <- still a configuration write
# font_system.cpp does exactly this, and without this pattern its publish could
# be deleted with the check still green -- measured, not supposed: that mutant
# was the one miss in the sweep that produced this rule.
SUBSTRUCT_BIND = re.compile(
    r"auto&\s*(?P<name>\w+)\s*=\s*[\w:]*ThemeDefaults::get\(\)"
    r"\s*\.\s*theme\s*\.\s*\w+\s*;"
)
PUBLISH = "publish_app_theme()"

# Reading is always fine; only assignment is a configuration act.
READ_ONLY_FILES = {
    "src/ui/viewport.h",       # reads ui_scale
    "src/ui/font_system.h",    # declarations only
    "src/ui/theme_config.h",   # the helper itself
}

SEARCH_ROOTS = ("src",)


def source_files() -> list[Path]:
    out = []
    for root in SEARCH_ROOTS:
        for path in sorted((ROOT / root).rglob("*")):
            if path.suffix in {".h", ".cpp", ".mm"} and path.is_file():
                out.append(path)
    return out


def enclosing_function(text: str, index: int) -> tuple[int, int]:
    """The brace-delimited block holding `index`, as [start, end)."""
    depth = 0
    start = 0
    for i in range(index, -1, -1):
        if text[i] == "}":
            depth += 1
        elif text[i] == "{":
            if depth == 0:
                start = i
                break
            depth -= 1
    depth = 0
    end = len(text)
    for i in range(start + 1, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            if depth == 0:
                end = i
                break
            depth -= 1
    return start, end


def check() -> list[str]:
    problems: list[str] = []
    for path in source_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel in READ_ONLY_FILES:
            continue
        text = path.read_text()
        sites: list[tuple[int, str]] = [
            (m.start(), m.group("field")) for m in WRITE.finditer(text)
        ]
        for bind in list(ALIAS_BIND.finditer(text)) + list(
            SUBSTRUCT_BIND.finditer(text)
        ):
            name = bind.group("name")
            for m in re.finditer(rf"\b{name}\s*\.\s*(?P<field>\w+)\s*=", text):
                if m.start() > bind.start():
                    sites.append((m.start(), m.group("field")))
        for index, field in sites:
            start, end = enclosing_function(text, index)
            if PUBLISH in text[start:end]:
                continue
            line = text.count("\n", 0, index) + 1
            problems.append(
                f"{rel}:{line}: writes ThemeDefaults theme.{field} without "
                f"hanabi::ui::publish_app_theme() in the same scope — "
                f"begin_frame() will revert it on the next frame"
            )
    return problems


def selftest() -> int:
    cases = [
        ("bare write", "void f() { ThemeDefaults::get().theme.ui_scale = 2.f; }", False),
        (
            "paired write",
            "void f() { ThemeDefaults::get().theme.ui_scale = 2.f;"
            " hanabi::ui::publish_app_theme(); }",
            True,
        ),
        (
            "alias write",
            "void f() { auto& theme = ui::imm::ThemeDefaults::get().theme;"
            " theme.roundness = 0.f; }",
            False,
        ),
        (
            "alias write paired",
            "void f() { auto& theme = ui::imm::ThemeDefaults::get().theme;"
            " theme.roundness = 0.f; hanabi::ui::publish_app_theme(); }",
            True,
        ),
        (
            "read is fine",
            "float g() { return ui::imm::ThemeDefaults::get().theme.ui_scale; }",
            True,
        ),
    ]
    failed = []
    for name, body, should_pass in cases:
        sites: list[tuple[int, str]] = [
            (m.start(), m.group("field")) for m in WRITE.finditer(body)
        ]
        for bind in list(ALIAS_BIND.finditer(body)) + list(
            SUBSTRUCT_BIND.finditer(body)
        ):
            alias = bind.group("name")
            for m in re.finditer(rf"\b{alias}\s*\.\s*(?P<field>\w+)\s*=", body):
                if m.start() > bind.start():
                    sites.append((m.start(), m.group("field")))
        ok = True
        for index, _ in sites:
            start, end = enclosing_function(body, index)
            if PUBLISH not in body[start:end]:
                ok = False
        if ok != should_pass:
            failed.append(name)
    if failed:
        print("check_theme_config selftest: FAIL: " + ", ".join(failed))
        return 1
    print(f"check_theme_config selftest: {len(cases)} cases passed")
    return 0


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest()
    problems = check()
    if problems:
        print("check_theme_config: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("check_theme_config: every ThemeDefaults theme write is published to "
          "app_default")
    return 0


if __name__ == "__main__":
    sys.exit(main())
