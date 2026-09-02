#!/usr/bin/env python3
"""Guard Home's catalog census, revision contract, and pointer lifetime."""

from __future__ import annotations

from dataclasses import dataclass
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = "src/ecs/main_pane_system.h"
COMPONENTS = "src/ecs/components.h"
ACCESS_BASELINE = {"render_digest": 1, "render_home": 2}
WALK_BASELINE = {"render_digest": 1}

SIGNATURE = re.compile(
    r"^    (?:[A-Za-z_][\w:<>,*& ]*?[ *&])?([A-Za-z_]\w*)\(", re.M
)
CATALOG_ACCESS = re.compile(
    r"\b(?P<receiver>[A-Za-z_]\w*)\s*(?:\.|->)\s*sessions\b"
)
CATALOG_ALIAS = re.compile(
    r"\b(?:const\s+)?auto\s*(?:const\s*)?[&*]+\s*(?P<alias>[A-Za-z_]\w*)"
    r"\s*=\s*(?P<receiver>[A-Za-z_]\w*)\s*(?:\.|->)\s*sessions\b"
)
RANGE_FOR = re.compile(r"\bfor\s*\([^;{}]*:\s*(?P<range>[^)]+)\)")
MEMBER_CALL = re.compile(
    r"\b(?P<receiver>[A-Za-z_]\w*)\s*(?:\.|->)\s*"
    r"(?P<method>[A-Za-z_]\w*)\s*\("
)
UNQUALIFIED_CALL = re.compile(r"(?<![A-Za-z0-9_:.>])([A-Za-z_]\w*)\s*\(")
REVISION_WRITE = re.compile(
    r"(?:\+\+|--)\s*sessionCatalogRevision\b|"
    r"\bsessionCatalogRevision\s*(?:\+\+|--|[+\-*/]?=)"
)
RAW_STRING = re.compile(r'R"([^\s()\\]{0,16})\(')
CHAR_LITERAL = re.compile(r"'(?:\\.|[^\\'])'")


@dataclass(frozen=True)
class Function:
    name: str
    start: int
    brace: int
    end: int


def blank_comments_and_literals(text: str) -> str:
    out = list(text)
    i = 0
    while i < len(text):
        if text[i : i + 2] == "//":
            while i < len(text) and text[i] != "\n":
                out[i] = " "
                i += 1
        elif text[i : i + 2] == "/*":
            while i < len(text) and text[i : i + 2] != "*/":
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for j in range(i, min(i + 2, len(text))):
                out[j] = " "
            i += 2
        elif text[i] == "R" and RAW_STRING.match(text, i):
            match = RAW_STRING.match(text, i)
            closing = ")" + match.group(1) + '"'
            end = text.find(closing, i)
            end = len(text) if end < 0 else end + len(closing)
            for j in range(i, end):
                if text[j] != "\n":
                    out[j] = " "
            i = end
        elif text[i] == '"':
            out[i] = " "
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < len(text):
                        out[i] = " "
                        i += 1
                elif text[i] == '"':
                    out[i] = " "
                    i += 1
                    break
                else:
                    if text[i] != "\n":
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


def matching_brace(code: str, opening: int) -> int:
    depth = 0
    for i in range(opening, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(code)


def function_spans(code: str, start: int = 0, end: int | None = None) -> list[Function]:
    end = len(code) if end is None else end
    out = []
    for match in SIGNATURE.finditer(code, start, end):
        name = match.group(1)
        if name in {"if", "for", "while", "switch", "return", "catch"}:
            continue
        brace = code.find("{", match.end(), end)
        if brace < 0 or ";" in code[match.end() : brace]:
            continue
        close = matching_brace(code, brace)
        if close <= end:
            out.append(Function(name, match.start(), brace, close))
    return out


def holder_of(functions: list[Function], pos: int) -> str:
    holder = "<global>"
    width = None
    for function in functions:
        if function.start <= pos <= function.end:
            candidate = function.end - function.start
            if width is None or candidate < width:
                holder = function.name
                width = candidate
    return holder


def calls_in(body: str, known: set[str]) -> set[str]:
    return {match.group(1) for match in UNQUALIFIED_CALL.finditer(body)
            if match.group(1) in known}


def reachable_from(root: str, functions: list[Function], code: str) -> set[str]:
    names = {function.name for function in functions}
    bodies: dict[str, list[str]] = {}
    for function in functions:
        bodies.setdefault(function.name, []).append(code[function.brace:function.end])
    seen = set()
    pending = [root]
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        for body in bodies.get(name, []):
            pending.extend(calls_in(body, names) - seen)
    return seen


def struct_body(code: str, name: str) -> tuple[int, int]:
    match = re.search(r"\bstruct\s+" + re.escape(name) + r"\b[^;{]*\{", code)
    if match is None:
        raise ValueError(f"cannot find struct {name}")
    opening = code.find("{", match.start())
    return opening, matching_brace(code, opening)


def app_catalog_methods() -> tuple[set[str], list[str]]:
    text = (ROOT / COMPONENTS).read_text()
    code = blank_comments_and_literals(text)
    start, end = struct_body(code, "AppComponent")
    methods = function_spans(code, start, end)
    names = {method.name for method in methods}
    bodies: dict[str, str] = {
        method.name: code[method.brace:method.end] for method in methods
    }
    calls = {name: calls_in(body, names) for name, body in bodies.items()}
    session_writers = {
        method.name for method in methods
        if re.search(r"\bsessions\b", bodies[method.name])
        and re.search(r"\)\s*const\b", code[method.start:method.brace]) is None
    }
    revision_writers = {
        name for name, body in bodies.items() if REVISION_WRITE.search(body)
    }

    reaches_revision = set(revision_writers)
    changed = True
    while changed:
        changed = False
        for name, callees in calls.items():
            if name not in reaches_revision and callees & reaches_revision:
                reaches_revision.add(name)
                changed = True

    problems = []
    for name in sorted(session_writers - reaches_revision):
        problems.append(
            f"{COMPONENTS}: non-const AppComponent::{name}() touches sessions "
            "without advancing sessionCatalogRevision"
        )

    mutators = set(session_writers) | set(revision_writers)
    changed = True
    while changed:
        changed = False
        for name, callees in calls.items():
            if name not in mutators and callees & mutators:
                mutators.add(name)
                changed = True
    return mutators, problems


def catalog_census(text: str, code: str, functions: list[Function]) -> tuple[dict[str, int], dict[str, int]]:
    accesses: dict[str, int] = {}
    for match in CATALOG_ACCESS.finditer(code):
        holder = holder_of(functions, match.start())
        accesses[holder] = accesses.get(holder, 0) + 1

    walks: dict[str, int] = {}
    for function in functions:
        body = code[function.brace:function.end]
        aliases = {match.group("alias") for match in CATALOG_ALIAS.finditer(body)}
        count = 0
        for match in RANGE_FOR.finditer(body):
            expression = match.group("range").strip()
            if CATALOG_ACCESS.search(expression) or expression in aliases:
                count += 1
        if count:
            walks[function.name] = walks.get(function.name, 0) + count
    return accesses, walks


def compare_census(kind: str, seen: dict[str, int], baseline: dict[str, int]) -> list[str]:
    problems = []
    for holder in sorted(set(seen) | set(baseline)):
        actual = seen.get(holder, 0)
        expected = baseline.get(holder, 0)
        if actual != expected:
            problems.append(
                f"{TARGET}: {holder}() has {actual} session-catalog {kind}, "
                f"baseline {expected}"
            )
    return problems


def main() -> int:
    text = (ROOT / TARGET).read_text()
    code = blank_comments_and_literals(text)
    functions = function_spans(code)
    accesses, walks = catalog_census(text, code, functions)
    problems = compare_census("accesses", accesses, ACCESS_BASELINE)
    problems += compare_census("walks", walks, WALK_BASELINE)

    mutators, component_problems = app_catalog_methods()
    problems += component_problems
    reachable = reachable_from("render_home", functions, code)
    for function in functions:
        if function.name not in reachable:
            continue
        body = code[function.brace:function.end]
        for match in MEMBER_CALL.finditer(body):
            if match.group("method") in mutators:
                line = text.count("\n", 0, function.brace + match.start()) + 1
                problems.append(
                    f"{TARGET}:{line}: Home-reachable {function.name}() calls "
                    f"catalog mutator {match.group('method')}() through receiver "
                    f"{match.group('receiver')}"
                )

    if problems:
        for problem in problems:
            print(f"check_home_scan: {problem}", file=sys.stderr)
        return 1

    access_text = ", ".join(
        f"{name}={count}" for name, count in sorted(accesses.items())
    )
    walk_text = ", ".join(
        f"{name}={count}" for name, count in sorted(walks.items())
    )
    print(
        f"check_home_scan: accesses [{access_text}]; walks [{walk_text}]; "
        "Home-reachable code calls no revision-derived catalog mutator"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
