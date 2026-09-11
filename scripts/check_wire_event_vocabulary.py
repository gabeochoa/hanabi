#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests/fixtures/agentcloud/session_event_types.txt"
WIRE = ROOT / "src/api/agentcloud_client.cpp"
MOCK = ROOT / "src/api/mock_client.h"
DEMO_TAG = "quota_ledger_rebalanced"
TAG = re.compile(r"^[a-z][a-z0-9_.]*$")


def fail(*lines):
    for line in lines:
        print(f"wire-event-vocabulary: FAIL: {line}", file=sys.stderr)
    raise SystemExit(1)


def read(path):
    if not path.is_file():
        fail(f"{path.relative_to(ROOT)} is missing")
    text = path.read_text(encoding="utf-8")
    if not text.strip():
        fail(f"{path.relative_to(ROOT)} is empty")
    return text


def fixture_tags(text):
    tags = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not TAG.match(line):
            fail(f"fixture line is not a wire tag: {line!r}")
        tags.append(line)
    if len(tags) < 100:
        fail(f"fixture lists {len(tags)} tags; the platform ships over 100")
    if len(set(tags)) != len(tags):
        fail("fixture repeats a tag")
    return tags


def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        fail(f"{signature!r} is not in {WIRE.relative_to(ROOT)}")
    open_brace = source.find("{", start)
    depth = 0
    for i in range(open_brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace : i + 1]
    fail(f"{signature!r} never closes its braces")


def check(source, fixture):
    tags = fixture_tags(fixture)
    silent_body = function_body(
        source, "bool is_silent_wire_event(const std::string& type)")
    silent = re.findall(r'"([a-z][a-z0-9_.]*)"', silent_body)
    if not silent:
        fail("is_silent_wire_event lists no tags")
    parser = function_body(
        source, "std::vector<Message> parse_page_frames(const std::string& msg_json)")
    drawn = set(re.findall(r'type == "([a-z][a-z0-9_.]*)"', parser))
    if not drawn:
        fail("parse_page_frames draws no event type")
    fallback = re.search(
        r"else if \(!is_silent_wire_event\(type\)\)\s*\{\s*push_event\("
        r"EventKind::Unsupported,\s*sanitized_wire_tag\(type\)",
        parser)
    if fallback is None:
        fail("parse_page_frames no longer draws an unlisted tag as an "
             "Unsupported row through sanitized_wire_tag")

    problems = []
    silent_set = set(silent)
    if len(silent_set) != len(silent):
        problems.append("is_silent_wire_event repeats a tag")
    for tag in tags:
        in_silent = tag in silent_set
        in_drawn = tag in drawn
        if in_silent and in_drawn:
            problems.append(f"{tag} is both drawn and silent")
        elif not in_silent and not in_drawn:
            problems.append(f"{tag} is a shipped tag with no decision: it "
                            "would draw as unknown")
    for tag in sorted(silent_set - set(tags)):
        problems.append(f"{tag} is silenced but is not a shipped tag")
    for tag in sorted(drawn - set(tags)):
        problems.append(f"{tag} is drawn but is not a shipped tag")
    if DEMO_TAG in tags:
        problems.append(f"{DEMO_TAG} is the mock's unknown demo tag and is "
                        "now a shipped tag; the scene would lie")
    return problems


def expect_failure(label, probe):
    import contextlib
    import io

    try:
        with contextlib.redirect_stderr(io.StringIO()):
            probe()
    except SystemExit as stop:
        if stop.code == 1:
            return
        raise
    fail(f"selftest: {label} passed")


def selftest(source, fixture):
    if check(source, fixture):
        fail("selftest: the real tree does not pass")
    without_fallback = re.sub(
        r"\} else if \(!is_silent_wire_event\(type\)\) \{\s*push_event\("
        r"EventKind::Unsupported, sanitized_wire_tag\(type\), \"\"\);\s*\}",
        "}", source, count=1)
    if without_fallback == source:
        fail("selftest: could not plant the missing-fallback mutant")
    expect_failure("a parser without the Unsupported fallback",
                   lambda: check(without_fallback, fixture))
    silenced = source.replace('"task_progress",', "", 1)
    if silenced == source:
        fail("selftest: could not plant the forgotten-tag mutant")
    if not any("task_progress" in p for p in check(silenced, fixture)):
        fail("selftest: a forgotten shipped tag passed")
    expect_failure("an empty fixture",
                   lambda: check(source, "# nothing here\n"))
    undecided = check(source, fixture + "tag_invented_next_quarter\n")
    if not any("tag_invented_next_quarter" in p for p in undecided):
        fail("selftest: a shipped tag this build never decided passed")


def main():
    source = read(WIRE)
    fixture = read(FIXTURE)
    mock = read(MOCK)
    if f'"{DEMO_TAG}"' not in mock:
        fail(f"the mock no longer carries the {DEMO_TAG} demo row")
    selftest(source, fixture)
    problems = check(source, fixture)
    if problems:
        fail(*problems)
    print("wire-event-vocabulary: OK "
          f"({len(fixture_tags(fixture))} shipped tags, each drawn or silent)")


if __name__ == "__main__":
    main()
