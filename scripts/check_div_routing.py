#!/usr/bin/env python3
"""scripts/check_div_routing.py — the one line that keeps every widget cheap."""

from __future__ import annotations

import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMPORTS = "src/ecs/ui_imports.h"
WRAPPER = "src/ui/div.h"

BOUND = re.compile(r"^\s*using\s+hanabi::ui::div\s*;", re.M)
LIBRARY_BOUND = re.compile(r"^\s*using\s+afterhours::ui::imm::div\s*;", re.M)
LIBRARY_CALL = re.compile(r"afterhours::ui::imm::div\s*\(")
WRAPPER_CALL = re.compile(r"hanabi::ui::div\s*\(")
UNQUALIFIED_CALL = re.compile(r"(?<![A-Za-z0-9_:.>])div\s*\(")
INCLUDES_IMPORTS = re.compile(r'^\s*#\s*include\s*"[^"]*ui_imports\.h"', re.M)
CHAR_LITERAL = re.compile(r"'(\\.|[^\\'])'")
RAW_STRING = re.compile(r'R"([^\s()\\]{0,16})\(')


class Source:
    def __init__(self, rel: str, text: str) -> None:
        self.rel = rel
        self.text = text
        self.code = blank_comments_and_literals(text)
        self.starts = [0] + [m.end() for m in re.finditer("\n", text)]

    def line(self, offset: int) -> int:
        return bisect.bisect_right(self.starts, offset)

    def at(self, offset: int) -> str:
        return "%s:%d" % (self.rel, self.line(offset))


def blank_comments_and_literals(text: str) -> str:
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif two == "/*":
            while i < n and text[i : i + 2] != "*/":
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for j in range(i, min(i + 2, n)):
                out[j] = " "
            i += 2
        elif text[i] == "R" and RAW_STRING.match(text, i):
            delimiter = RAW_STRING.match(text, i).group(1)
            closing = ")%s\"" % delimiter
            end = text.find(closing, i)
            end = n if end < 0 else end + len(closing)
            for j in range(i, end):
                if text[j] != "\n":
                    out[j] = " "
            i = end
        elif text[i] == '"':
            out[i] = " "
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n:
                    if text[i] != "\n":
                        out[i] = " "
                    i += 1
            if i < n:
                out[i] = " "
                i += 1
        elif text[i] == "'" and CHAR_LITERAL.match(text, i):
            end = CHAR_LITERAL.match(text, i).end()
            for j in range(i, end):
                out[j] = " "
            i = end
        else:
            i += 1
    return "".join(out)


def load_sources() -> list[Source]:
    paths = sorted((ROOT / "src").rglob("*.h")) + sorted(
        (ROOT / "src").rglob("*.cpp")
    )
    return [
        Source(p.relative_to(ROOT).as_posix(), p.read_text()) for p in paths
    ]


def call_arguments(code: str, open_paren: int) -> tuple[list[str], int]:
    args: list[str] = []
    depth = 0
    start = open_paren + 1
    i = open_paren
    n = len(code)
    while i < n:
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(code[start:i])
                return args, i
        elif c == "," and depth == 1:
            args.append(code[start:i])
            start = i + 1
        i += 1
    return args, -1


def enclosing_block_end(code: str, offset: int) -> int:
    depth = 0
    i = offset
    n = len(code)
    while i < n:
        c = code[i]
        if c == "{":
            depth += 1
        elif c == "}":
            if depth == 0:
                return i
            depth -= 1
        i += 1
    return n


def enclosing_block_start(code: str, offset: int) -> int:
    depth = 0
    i = offset - 1
    while i >= 0:
        c = code[i]
        if c == "}":
            depth += 1
        elif c == "{":
            if depth == 0:
                return i
            depth -= 1
        i -= 1
    return 0


def binding_problems(imports: str) -> list[str]:
    problems = []
    if not BOUND.search(imports):
        problems.append("%s: `using hanabi::ui::div;` is gone" % IMPORTS)
    if LIBRARY_BOUND.search(imports):
        problems.append("%s: binds div straight to afterhours" % IMPORTS)
    return problems


def library_call_problems(sources: list[Source]) -> list[str]:
    problems = []
    for src in sources:
        if src.rel == WRAPPER:
            continue
        for m in LIBRARY_CALL.finditer(src.code):
            problems.append(
                "%s: calls afterhours::ui::imm::div directly, "
                "bypassing the wrapper" % src.at(m.start())
            )
    return problems


def routing_problems(sources: list[Source]) -> list[str]:
    problems = []
    for src in sources:
        if src.rel in (WRAPPER, IMPORTS):
            continue
        calls = list(UNQUALIFIED_CALL.finditer(src.code))
        if not calls:
            continue
        if INCLUDES_IMPORTS.search(src.text) or BOUND.search(src.code):
            continue
        for m in calls:
            problems.append(
                "%s: unqualified div() with hanabi::ui::div not in scope — "
                "ADL resolves it to afterhours::ui::imm::div and the config "
                "is copied" % src.at(m.start())
            )
    return problems


def reuse_problems(sources: list[Source]) -> list[str]:
    problems = []
    for src in sources:
        if src.rel == WRAPPER:
            continue
        calls = [m for m in UNQUALIFIED_CALL.finditer(src.code)]
        calls += [m for m in WRAPPER_CALL.finditer(src.code)]
        for m in sorted(calls, key=lambda x: x.start()):
            open_paren = src.code.index("(", m.start())
            args, close = call_arguments(src.code, open_paren)
            if close < 0 or len(args) < 3:
                continue
            config = args[2].strip()
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", config):
                continue
            start = enclosing_block_start(src.code, m.start())
            declaration = re.search(
                r"\b(?:auto|ComponentConfig)\b[^;{}]*?\b%s\b\s*="
                % re.escape(config),
                src.code[start : m.start()],
            )
            if declaration is None:
                problems.append(
                    "%s: `%s` is declared outside the block that passes it to "
                    "div — div MOVES it, so a second pass through this block "
                    "hands over an emptied config" % (src.at(m.start()), config)
                )
                continue
            end = enclosing_block_end(src.code, close)
            after = src.code[close:end]
            reuse = re.search(r"\b%s\b" % re.escape(config), after)
            if reuse is None:
                continue
            problems.append(
                "%s: `%s` is used again at %s after div consumed it — div "
                "MOVES a named ComponentConfig, so the second use carries an "
                "emptied label"
                % (src.at(m.start()), config, src.at(close + reuse.start()))
            )
    return problems


def why_binding() -> None:
    print()
    print("  afterhours' div() takes its ComponentConfig BY VALUE, and the")
    print("  fluent builder returns ComponentConfig& -- so an inline")
    print("  `ComponentConfig{}.with_label(...)` chain is an LVALUE and the")
    print("  parameter is COPY-constructed. Every std::string the config")
    print("  carries past libc++'s 22-character small-string buffer is one")
    print("  more malloc, per widget, per frame. hanabi::ui::div moves")
    print("  instead. Measured on the frozen base binary: home20 829 -> 740,")
    print("  home2000 1181 -> 1034, thread480 2707 -> 2599 allocations per")
    print("  frame (scripts/alloc_gate.sh, docs/perf/ALLOCATIONS.md).")
    print()
    print("  The binding is one `using` in %s and it reads as a" % IMPORTS)
    print("  redundant alias. Deleting it costs the app those allocations")
    print("  back, changes not one pixel, and no other check sees it:")
    print("  tests/unit/test_div_move.cpp proves the WRAPPER still moves,")
    print("  and the alloc gate's 20% headroom is wider than the loss.")
    print()
    print("  An unqualified div() in a file that does not include")
    print("  %s is the same regression wearing a disguise:" % IMPORTS)
    print("  ComponentConfig lives in afterhours::ui::imm, so that namespace")
    print("  is an associated one and ADL finds the library's div with no")
    print("  `using` anywhere, no compile error, and no pixel changed.")
    print("  Include the imports header, or write hanabi::ui::div explicitly.")


def why_reuse() -> None:
    print()
    print("  hanabi::ui::div MOVES the ComponentConfig it is handed -- that")
    print("  is the whole point of the wrapper, and it applies to a NAMED")
    print("  config just as much as to an inline chain. A named config is")
    print("  therefore CONSUMED by the call: after it, the variable holds a")
    print("  moved-from config whose heap-allocated label is empty. Nothing")
    print("  warns, nothing crashes, and the second widget renders blank.")
    print()
    print("  Build a fresh ComponentConfig for the second call, or inline")
    print("  the chain into each. A config declared outside the block that")
    print("  divs it is the same bug spread over iterations: the second")
    print("  pass through the block hands over what the first emptied.")
    print("  tests/unit/test_div_move.cpp pins this contract in")
    print("  a_named_config_is_consumed_by_div.")


def main() -> int:
    sources = load_sources()
    by_rel = {src.rel: src for src in sources}

    binding = binding_problems(by_rel[IMPORTS].code)
    binding += library_call_problems(sources)
    binding += routing_problems(sources)
    reuse = reuse_problems(sources)

    if not binding and not reuse:
        print("check_div_routing: ok (div routed through hanabi::ui::div, "
              "no named config reused after it)")
        return 0

    print("check_div_routing: FAIL")
    for p in binding + reuse:
        print("  %s" % p)
    if binding:
        why_binding()
    if reuse:
        why_reuse()
    return 1


if __name__ == "__main__":
    sys.exit(main())
