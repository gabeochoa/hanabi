#!/usr/bin/env python3
"""scripts/gate_audit.py — break every gate on purpose, and say what it did.

WHY THIS EXISTS. Two gates in this repo were found to have stopped asserting
anything, and both were found by luck: perf_transcript_slope.sh went
permanently green when a fix rerouted its work to a new function, and
scaling_gate.sh asserted nothing at all for a while after sidebar
virtualization landed. A gate that cannot fail is worse than no gate -- it
occupies the slot, it costs the wall clock, and it is read as evidence.

So this is the audit, as a program rather than as an afternoon. Most entries
below are DEFECTS: a one-line patch to a source file (or an env var), the gate
it must turn red, and nothing else. Three structured scaling blind spots instead
expect rc 0 and record the separate gate that covers the missing property. Rc 1
is a caught defect; rc 2 is incomplete infrastructure and never passes. The
harness applies each mutation, force-rebuilds, records output under
`/tmp/hanabi_gate_audit/`, restores source, and force-rebuilds again.

    scripts/gate_audit.py --list
    scripts/gate_audit.py                 # everything, ~25 minutes
    scripts/gate_audit.py scroll.blocks soak.cpu

The results table lives in docs/perf/GATES.md section 0 and this file is what
regenerates it. When a gate's thresholds move, or a defect stops applying
because the code it patches moved, THAT is the signal: an anchor that no longer
matches is a gate whose subject has changed underneath it.

TWO THINGS THIS HARNESS LEARNED THE HARD WAY.

  * It REBUILDS after restoring. The first version put the source back and
    left the defective binary in output/, and the next clean soak_gate.sh run
    read 6,883 entities and 8.7 ms a frame off it. scripts/fresh.sh exists for
    exactly this and only warns.
  * "The gate went red" is not the result. WHICH ROW went red is the result.
    An unbounded version of the entities defect grew the app to 151 ms a frame
    and was killed by soak_gate's own watchdog, so what went red was "the run
    ended before it reached a verdict" -- correct, and not the arm under test.
    Every defect here is sized so the gate survives measuring it.
"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = "/tmp/hanabi_gate_audit"
os.makedirs(OUT, exist_ok=True)

SB = "src/ecs/sidebar_system.h"
SBB = "src/ecs/sidebar_buckets.h"
HB = "src/ecs/home_buckets.h"
COMP = "src/ecs/components.h"
MP = "src/ecs/main_pane_system.h"
WR = "src/ecs/widget_retire_system.h"
TH = "src/ui/theme.h"
FC = "src/ui/field_chrome.h"
FMT = "src/util/format.h"
ST = "src/ui/snippet_text.h"

LEAK_ANCHOR = """    void once(float) override {
        hanabi::widget_epoch::configure_retirement();"""

DEFECTS = {
    # ---- soak_gate ------------------------------------------------------
    "soak.bytes": dict(
        gate="soak-gate", build="app",
        patches=[(WR, LEAK_ANCHOR, LEAK_ANCHOR + """
        static std::vector<std::string> g_leak;
        g_leak.emplace_back(512, 'x');""")]),
    "soak.entities": dict(
        gate="soak-gate", build="app", env={"HANABI_RETIRE": "0"},
        patches=[(SB, '        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",',
                  '        shown += render_folder(ctx, scroll.ent(),\n'
                  '                               900000 + 100 * static_cast<int>(\n'
                  '                                   hanabi::widget_epoch::epoch() % 400u),\n'
                  '                               "", "recent",')]),
    "cover.retire_entities": dict(
        gate="retire-gate", build="app", env={"HANABI_RETIRE": "0"},
        patches=[(SB, '        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",',
                  '        shown += render_folder(ctx, scroll.ent(),\n'
                  '                               900000 + 100 * static_cast<int>(\n'
                  '                                   hanabi::widget_epoch::epoch() % 400u),\n'
                  '                               "", "recent",')]),
    "cover.scaling_entities": dict(
        gate="scaling-gate", build="app", env={"HANABI_RETIRE": "0"},
        expected_rc=0,
        blind_spot=dict(
            missed="a catalog-independent bounded widget plateau",
            rationale="catalog scaling compares two catalog sizes, so the same plateau divides out",
            covered_by="soak-gate entity level and retire-gate stale/live counts",
        ),
        patches=[(SB, '        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",',
                  '        shown += render_folder(ctx, scroll.ent(),\n'
                  '                               900000 + 100 * static_cast<int>(\n'
                  '                                   hanabi::widget_epoch::epoch() % 400u),\n'
                  '                               "", "recent",')]),
    "soak.entities_unbounded": dict(
        gate="soak-gate", build="app", env={"HANABI_RETIRE": "0"},
        patches=[(SB, '        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",',
                  '        shown += render_folder(ctx, scroll.ent(),\n'
                  '                               900000 + 40 * static_cast<int>(\n'
                  '                                   hanabi::widget_epoch::epoch() / 25u),\n'
                  '                               "", "recent",')]),
    # The regression the COUNTER gate is blind to: a raw walk of the catalog
    # inside render_folder, which never enters SidebarBuckets and so is never
    # counted by it. Caught directly by check_sidebar_scan.py.
    "sidebar.raw_rescan": dict(
        gate="sidebar-source", build="none",
        patches=[(SB, '        // Hide a folder with no (matching) members. With an active query this\n        // is what drops non-matching folders out of the tree.\n        if (members.empty()) {', '        for (const auto& s : app.sessions) {\n            if (s.folder == key) members.push_back(&s);\n        }\n        // Hide a folder with no (matching) members. With an active query this\n        // is what drops non-matching folders out of the tree.\n        if (members.empty()) {')]),
    # ---- sidebar_scan_gate ----------------------------------------------
    # The scan the one-pass collection replaced: collect again for every
    # folder, defeating the kept answer with the frame epoch. The RATIO arm is
    # the one this must turn red -- arm A has no folders and stays green.
    "sidebar.per_folder_scan": dict(
        gate="sidebar-scan-gate", build="app",
        patches=[(SB, '            for (const auto& folder : folderNames_) {\n                shown += render_folder(', '            for (const auto& folder : folderNames_) {\n                buckets_.rebuild(\n                    app->sessionCatalogRevision +\n                        hanabi::widget_epoch::epoch(),\n                    app->sessions, q,\n                    app->collapsedFolders.count(kHideAutoKey) > 0,\n                    [](const std::string& id, const std::string& needle) {\n                        return api::disk_cache::content_matches(id, needle);\n                    });\n                shown += render_folder(')]),
    # The kept answer alone: one pass, but on every frame. Only the LEVEL arms
    # (rebuilds, reuse) can see this one; the ratio stays 1.0.
    "sidebar.no_memo": dict(
        gate="sidebar-scan-gate", build="app",
        patches=[(SBB, '        if (valid_ && q.empty() && query_.empty() &&\n            revision_ == catalogRevision && hideAutomated_ == hideAutomated) {', '        if (false && valid_ && q.empty() && query_.empty() &&\n            revision_ == catalogRevision && hideAutomated_ == hideAutomated) {')]),
    "home.no_memo": dict(
        gate="home-scan-gate", build="app",
        patches=[(HB, '        if (valid_ && revision_ == catalogRevision) {',
                  '        if (false && valid_ && revision_ == catalogRevision) {')]),
    "home.raw_rescan": dict(
        gate="home-source", build="none",
        patches=[(MP, '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);',
                  '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);\n'
                  '        for (const auto& session : app.sessions)\n'
                  '            if (session.id.empty()) homeMatched_ += 0;')]),
    "home.inline_mutation": dict(
        gate="home-source", build="none",
        patches=[(MP, '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);',
                  '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);\n'
                  '        app.replace_sessions(app.sessions);')]),
    "home.helper_alias_walk": dict(
        gate="home-source", build="none",
        patches=[(MP, '    // ---------------- Home digest ------------------------------------------',
                  '    void audit_home_catalog_helper(AppComponent& owner) {\n'
                  '        const auto& catalog = owner.sessions;\n'
                  '        for (const auto& session : catalog) (void)session;\n'
                  '    }\n\n'
                  '    // ---------------- Home digest ------------------------------------------'),
                 (MP, '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);',
                  '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);\n'
                  '        audit_home_catalog_helper(app);')]),
    "home.alias_new_mutator": dict(
        gate="home-source", build="none",
        patches=[(COMP, '    void replace_sessions(std::vector<api::SessionSummary> replacement) {',
                  '    void audit_touch_catalog() {\n'
                  '        sessions.clear();\n'
                  '        mark_session_catalog_changed();\n'
                  '    }\n\n'
                  '    void replace_sessions(std::vector<api::SessionSummary> replacement) {'),
                 (MP, '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);',
                  '        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);\n'
                  '        auto& owner = app;\n'
                  '        owner.audit_touch_catalog();')]),
    "home.unversioned_mutator": dict(
        gate="home-source", build="none",
        patches=[(COMP, '    void replace_sessions(std::vector<api::SessionSummary> replacement) {',
                  '    void audit_unversioned_catalog_mutation() {\n'
                  '        sessions.clear();\n'
                  '    }\n\n'
                  '    void replace_sessions(std::vector<api::SessionSummary> replacement) {')]),
    "digest.home_window": dict(
        gate="digest-gate", build="app",
        patches=[(MP, """        const digest::CardWindow win = digest::section_window(""",
                  """        digest::CardWindow win = digest::section_window(""")
                 ,(MP, """        const int base = homeMatched_;""",
                  """        win.first = 0;
        win.last = static_cast<int>(n);
        win.above = 0.0f;
        win.below = 0.0f;
        const int base = homeMatched_;""")]),
    "alloc.home_window": dict(
        gate="alloc-gate", build="app",
        patches=[(MP, """        const digest::CardWindow win = digest::section_window(
            static_cast<int>(n),
            [this](int k) { return pitches_[static_cast<size_t>(k)]; }, viewH,
            offsetY, targetY, sectionY);""",
                  """        const digest::CardWindow win = digest::card_window(
            static_cast<int>(n),
            [this](int k) { return pitches_[static_cast<size_t>(k)]; }, 0.0f,
            0.0f, 0.0f);""")]),
    # The case-insensitive substring scan put back the way it was spelled
    # everywhere before contains_lower: build a lowercased copy of the
    # HAYSTACK, then find() in it. One malloc per candidate per frame, which
    # both filter arms of alloc-gate read as a level that scales with the
    # catalog. Nothing else changes and no answer changes; the differential
    # arm of tests/unit/test_contains_lower.cpp stays green against it, which
    # is why the gate and the test are both here.
    #
    # It anchors on find_lower rather than contains_lower because the offset-
    # returning form is now the one scan both spellings share; contains_lower
    # is expressed in terms of it. The anchor moved when that happened and was
    # not updated, so apply() aborted the whole run at this entry and silently
    # skipped the 21 defects after it.
    "alloc.ci_copies_haystack": dict(
        gate="alloc-gate", build="app",
        patches=[(FMT, """    if (lowerNeedle.empty()) return 0;
    if (lowerNeedle.size() > hay.size()) return std::string_view::npos;
    const char first = lowerNeedle.front();""",
                  """    if (lowerNeedle.empty()) return 0;
    if (lowerNeedle.size() > hay.size()) return std::string_view::npos;
    return to_lower(std::string(hay)).find(lowerNeedle);
    const char first = lowerNeedle.front();""")]),
    # The snippet's CUT, put back the way it was spelled before extract_into:
    # lower a copy of the whole line, then find() in that. One malloc per
    # message scanned per frame.
    #
    # Its gate is the unit test and NOT alloc-gate, which is the whole reason
    # it is a separate entry: reverting this alone reads 1009.8 on the
    # search2000 arm against a 1130 ceiling — 89%, green. A frame-level
    # ceiling cannot hold a property this size, and the honest place to gate
    # it is where the cut is measured directly.
    "alloc.snippet_copies_line": dict(
        gate="snippet-alloc", build="none",
        patches=[(ST, "    const size_t at = fmtutil::find_lower(text, lowerQuery);",
                  "    const size_t at = "
                  "fmtutil::to_lower(std::string(text)).find(lowerQuery);")]),
    "alloc.snippet_captures_strings": dict(
        gate="alloc-gate", build="app",
        patches=[(SB, """        const auto ep = mk(parent, 2);
        auto& sd = ep.first.get().addComponentIfMissing<ecs::LineDrawState>();
        sd.text = text;
        sd.query = q;
        ecs::LineDrawState* sdp = &sd;
        div(ctx, ep,""",
                  """        div(ctx, mk(parent, 2),"""),
                 (SB, """                .with_on_draw_bg([sdp](RectangleType r) {
                    hanabi::snippet_highlight::draw(r, sdp->text, sdp->query,
                                                    theme::type::SM);
                })""",
                  """                .with_on_draw_bg([text, q](RectangleType r) {
                    hanabi::snippet_highlight::draw(r, text, q,
                                                    theme::type::SM);
                })""")]),
    "transcript.wrap": dict(
        gate="slope", build="app",
        patches=[(MP, "        if (const int* hit = memo.find(text, widthPx, fontPx)) {",
                  "        if (const int* hit = (const int*)nullptr) {")]),
    "soak.blocks": dict(
        gate="soak-gate", build="app",
        patches=[(WR, LEAK_ANCHOR, LEAK_ANCHOR + """
        static std::vector<char*> g_blocks;
        for (int i = 0; i < 4; ++i) g_blocks.push_back(new char[24]);""")]),
    "launch.latency": dict(
        gate="launch", build="app",
        patches=[(WR, LEAK_ANCHOR, LEAK_ANCHOR + """
        static bool g_once = true;
        if (g_once) { g_once = false; usleep(400000); }""")]),
    "soak.cpu": dict(
        gate="soak-gate", build="app",
        patches=[(WR, LEAK_ANCHOR, LEAK_ANCHOR + """
        static std::vector<int> g_walk;
        g_walk.push_back(1);
        volatile long g_sink = 0;
        for (int i = 0; i < 1200; ++i)
            for (int v : g_walk) g_sink += v;""")]),
    # ---- scaling_gate ---------------------------------------------------
    "scaling.widgets": dict(
        gate="scaling-gate", build="app", expected_rc=0,
        blind_spot=dict(
            missed="removing the sidebar cap while row virtualization remains",
            rationale="the window still bounds built rows, so scaling remains flat",
            covered_by="scroll-gate level arm directly removes row virtualization",
        ),
        patches=[(SB, "        return (expandedMore || total <= cap) ? total : cap;",
                  "        (void)expandedMore; (void)cap; return total;")]),
    "scaling.both": dict(
        gate="scaling-gate", build="app",
        patches=[(SB, "        return (expandedMore || total <= cap) ? total : cap;",
                  "        (void)expandedMore; (void)cap; return total;"),
                 (SB, "        if (!uniformHeight) return w;",
                  "        if (true) return w;\n        if (!uniformHeight) return w;")]),
    "scaling.virt_only": dict(
        gate="scaling-gate", build="app", expected_rc=0,
        blind_spot=dict(
            missed="removing row virtualization while the product cap remains",
            rationale="the cap still bounds built rows, so scaling remains flat",
            covered_by="scroll-gate expands the list and fails when row_window is bypassed",
        ),
        patches=[(SB, "        if (!uniformHeight) return w;",
                  "        if (true) return w;\n        if (!uniformHeight) return w;")]),
    # ---- scroll_gate ----------------------------------------------------
    "scroll.level": dict(
        gate="scroll-gate", build="app",
        patches=[(SB, "        if (!uniformHeight) return w;",
                  "        if (true) return w;\n        if (!uniformHeight) return w;")]),
    "scroll.cpu": dict(
        gate="scroll-gate", build="app",
        patches=[(SB, "        rowsRendered_ = win.last - win.first;",
                  "        {\n"
                  "            static std::vector<int> seen;\n"
                  "            for (int r = win.first; r < win.last; ++r) seen.push_back(r);\n"
                  "            volatile long sink = 0;\n"
                  "            for (int k = 0; k < 40; ++k)\n"
                  "                for (int v : seen) sink += v;\n"
                  "        }\n"
                  "        rowsRendered_ = win.last - win.first;")]),
    "scroll.blocks": dict(
        gate="scroll-gate", build="app",
        patches=[(SB, "            const int rowId = base + 1 + (++i);",
                  "            const int rowId = base + 1 + idx;")]),
    # ---- retire_gate ----------------------------------------------------
    "retire.stale": dict(gate="retire-gate", build="app", env={"HANABI_RETIRE": "0"}),
    # ---- composer_chrome_gate -------------------------------------------
    # The two regressions the multiline composer shipped over, one defect each.
    # Both are a deleted line, because both fixes ARE one line -- which is the
    # point of the gate: nothing else in the suite can see either of them.
    "chrome.fill": dict(
        gate="chrome-gate", build="app",
        patches=[(MP, "        hanabi::ui::field_chrome::clear_forced_fill(composerFieldId);\n",
                  "")]),
    "chrome.focus_edge": dict(
        gate="chrome-gate", build="app",
        patches=[(FC, "    if (!focused) return;", "    if (true) return;")]),
    # ---- perf_text_gate -------------------------------------------------
    "text.measures": dict(
        gate="text", build="app",
        patches=[(MP, "        if (const int* hit = memo.find(text, widthPx, fontPx)) {",
                  "        if (const int* hit = (const int*)nullptr) {")]),
    "text.advance_rate": dict(
        gate="text", build="app",
        patches=[(TH, "    if (const float* hit = memo.find(s, px, weightKey)) {",
                  "    if (const float* hit = (const float*)nullptr) {")]),
    "text.memo_bound": dict(
        gate="text", build="app",
        patches=[(MP, "        constexpr std::size_t kLineCountEntries = 512;",
                  "        constexpr std::size_t kLineCountEntries = 4096;")]),
    # ---- perf_transcript_slope ------------------------------------------
    "transcript.render_cache": dict(
        gate="slope", build="app",
        patches=[(MP, "            if (const auto* hit = render_cache().get(key, textW, m.text)) {",
                  "            if (const auto* hit = (const ecs::model::MsgRender*)nullptr) {")]),
    # ---- measure_launch -------------------------------------------------
    "launch.rss": dict(
        gate="launch", build="app",
        patches=[(WR, LEAK_ANCHOR, LEAK_ANCHOR + """
        static std::vector<std::string> g_big;
        if (g_big.empty())
            for (int i = 0; i < 400; ++i) g_big.emplace_back(1 << 20, 'x');""")]),
    # ---- the screenshot baselines ---------------------------------------
    # The two halves of the regression that motivated re-cutting this net: a
    # composer whose interior turned grey and lost its focus ring, on a tree
    # where 32 unit tests, 105 scripted UI tests and 9 perf gates were all
    # green. Both are afterhours_gaps.md #262/#263 reproduced in hanabi,
    # because vendor/afterhours is read-only.
    "shots.composer_grey": dict(
        gate="shots", build="app",
        patches=[(MP, """                .with_size(ComponentSize{percent(1.0f), pixels(kFieldH)})
                .with_transparent_bg()""",
                  """                .with_size(ComponentSize{percent(1.0f), pixels(kFieldH)})
                .with_custom_background(theme::Color{57, 57, 68, 255})""")]),
    "shots.focus_ring": dict(
        gate="shots", build="app",
        patches=[(MP, """        hanabi::ui::field_chrome::apply_focus_edge(
            inputWrap.ent().id, composerFocused, ctx.theme.accent);""",
                  """        hanabi::ui::field_chrome::apply_focus_edge(
            inputWrap.ent().id, false, ctx.theme.accent);""")]),
}

GATE_CMD = {
    "soak-gate": ["bash", "scripts/soak_gate.sh"],
    "scaling-gate": ["bash", "scripts/scaling_gate.sh"],
    "scroll-gate": ["bash", "scripts/scroll_gate.sh"],
    "retire-gate": ["bash", "scripts/retire_gate.sh"],
    "text": ["bash", "scripts/perf_text_gate.sh"],
    "chrome-gate": ["bash", "scripts/composer_chrome_gate.sh"],
    "slope": ["bash", "scripts/perf_transcript_slope.sh"],
    "digest-gate": ["bash", "scripts/digest_gate.sh"],
    "home-scan-gate": ["bash", "scripts/home_scan_gate.sh"],
    "home-source": ["/usr/bin/python3", "scripts/check_home_scan.py"],
    "sidebar-source": ["/usr/bin/python3", "scripts/check_sidebar_scan.py"],
    "sidebar-scan-gate": ["bash", "scripts/sidebar_scan_gate.sh"],
    "source-checks": ["make", "source-checks"],
    "alloc-gate": ["bash", "scripts/alloc_gate.sh"],
    # The snippet cut, measured directly. No frame-level ceiling can hold this
    # property (see alloc.snippet_copies_line), so its gate is the unit test.
    # The binary depends on every header, so make rebuilds it after a patch —
    # and a build failure exits 2, which the audit never accepts as a red.
    "snippet-alloc": ["bash", "-c",
                      "make output/tests/test_snippet_text && "
                      "output/tests/test_snippet_text"],
    "launch": ["bash", "scripts/measure_launch.sh"],
    # The screenshot subset `make test` runs. It captures and compares, so it
    # needs the built app and the machine to itself, like the UI suite.
    "shots": ["make", "validate-screenshots-fast"],
}

KNOWN_BLIND_SPOTS = {
    "cover.scaling_entities",
    "scaling.widgets",
    "scaling.virt_only",
}


def run(cmd, env=None, timeout=1200):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, env=e, capture_output=True, text=True,
                       timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def run_gate(gate, env=None):
    summary = os.path.join(ROOT, "test-failures", "summary-fast.json")
    if gate == "shots" and os.path.exists(summary):
        os.unlink(summary)
    raw_rc, out = run(GATE_CMD[gate], env)
    if gate != "shots" or raw_rc == 0:
        return raw_rc, out
    try:
        data = json.load(open(summary))
    except (OSError, ValueError):
        return 2, out + f"\n[audit] raw rc={raw_rc}; no valid screenshot summary\n"
    if data.get("failed", 0) > 0 or data.get("unbaselined_new"):
        return 1, out + f"\n[audit] raw rc={raw_rc}; normalized to rc=1 from screenshot evidence\n"
    return 2, out + f"\n[audit] raw rc={raw_rc}; summary did not prove a visual rejection\n"


def rc_matches(actual, expected):
    return actual in (0, 1) and actual == expected


def audit_selftest():
    checks = [
        (1, 1, True),
        (0, 0, True),
        (0, 1, False),
        (1, 0, False),
        (2, 1, False),
        (2, 0, False),
        (3, 1, False),
    ]
    failed = False
    for actual, expected, want in checks:
        got = rc_matches(actual, expected)
        observed = "accepted" if got else "rejected"
        wanted = "accepted" if want else "rejected"
        print(f"  rc={actual} expected_rc={expected}: {observed}; "
              f"wanted {wanted} — {'PASS' if got == want else 'FAIL'}")
        failed |= got != want
    blind = {name for name, defect in DEFECTS.items()
             if defect.get("expected_rc", 1) == 0}
    missing = sorted(name for name in blind if "blind_spot" not in DEFECTS[name])
    if missing:
        print("  missing blind_spot metadata: " + ", ".join(missing))
        failed = True
    if blind != KNOWN_BLIND_SPOTS:
        print("  expected-green set differs: " + ", ".join(sorted(blind)))
        failed = True
    if any(defect.get("expected_rc", 1) not in (0, 1)
           for defect in DEFECTS.values()):
        print("  expected_rc may only be 0 or 1")
        failed = True
    if failed:
        print("gate-audit --selftest: FAIL")
        return 1
    print("gate-audit --selftest: PASS — rc=2 is never accepted")
    return 0


def build(kind):
    if kind == "none":
        return True, ""
    if kind == "uitest":
        rc, out = run(["make", "-B", "uitest-build"])
    else:
        rc, out = run(["make", "-B", "-j8"])
    return rc == 0, out[-2000:]


def apply(patches):
    saved = {}
    for path, old, new in patches:
        full = os.path.join(ROOT, path)
        if path not in saved:
            saved[path] = open(full).read()
        s = open(full).read()
        if s.count(old) != 1:
            for p, c in saved.items():
                open(os.path.join(ROOT, p), "w").write(c)
            raise SystemExit(f"anchor not unique in {path}: {old[:60]!r}")
        open(full, "w").write(s.replace(old, new))
    return saved


def restore(saved):
    for p, c in saved.items():
        open(os.path.join(ROOT, p), "w").write(c)


def main():
    if "--selftest" in sys.argv:
        return audit_selftest()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if "--list" in sys.argv:
        for name, defect in DEFECTS.items():
            expected = defect.get("expected_rc", 1)
            suffix = " known-blind-spot" if expected == 0 else ""
            print(f"{name:28s} -> {defect['gate']} rc={expected}{suffix}")
        return 0
    names = args or list(DEFECTS)
    results = {}
    failed = []
    for name in names:
        d = DEFECTS[name]
        expected_rc = d.get("expected_rc", 1)
        print(f"=== {name}", flush=True)
        saved = apply(d.get("patches", []))
        try:
            ok, blog = build(d["build"])
            if not ok:
                results[name] = {"build": "FAILED", "log": blog,
                                 "passed": False}
                failed.append(name)
                print("  BUILD FAILED\n" + blog)
                continue
            rc, out = run_gate(d["gate"], d.get("env"))
            passed = rc_matches(rc, expected_rc)
            results[name] = {"rc": rc, "expected_rc": expected_rc,
                             "passed": passed, "out": out}
            if "blind_spot" in d:
                results[name]["blind_spot"] = d["blind_spot"]
            open(f"{OUT}/{name}.log", "w").write(out)
            print(f"  rc={rc} expected_rc={expected_rc} "
                  f"{'PASS' if passed else 'FAIL'}")
            if not passed:
                failed.append(name)
        finally:
            restore(saved)
            # Rebuild on the way out. Leaving a defective binary in output/ is
            # how the next clean run reads as a catastrophic regression.
            build(d["build"] if d["build"] != "none" else "app")
    open(f"{OUT}/results.json", "w").write(json.dumps(results, indent=1))
    if failed:
        print("gate-audit: FAIL — " + ", ".join(failed))
        return 1
    print(f"gate-audit: PASS — {len(names)}/{len(names)} defects matched "
          "their expected outcomes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
