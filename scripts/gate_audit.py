#!/usr/bin/env python3
"""scripts/gate_audit.py — break every gate on purpose, and say what it did.

WHY THIS EXISTS. Two gates in this repo were found to have stopped asserting
anything, and both were found by luck: perf_transcript_slope.sh went
permanently green when a fix rerouted its work to a new function, and
scaling_gate.sh asserted nothing at all for a while after sidebar
virtualization landed. A gate that cannot fail is worse than no gate -- it
occupies the slot, it costs the wall clock, and it is read as evidence.

So this is the audit, as a program rather than as an afternoon. Each entry
below is a DEFECT: a one-line patch to a source file (or an env var), the gate
it should turn red, and nothing else. The harness applies it, rebuilds, runs
the gate, records the output under /tmp/hanabi_gate_audit/, and puts the source
back.

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
MP = "src/ecs/main_pane_system.h"
WR = "src/ecs/widget_retire_system.h"
TH = "src/ui/theme.h"
FV = "src/ecs/focus_visible_system.h"
FC = "src/ui/field_chrome.h"
FMT = "src/util/format.h"

LEAK_ANCHOR = """    void once(float) override {
        hanabi::widget_epoch::begin_epoch();"""

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
    # counted by it. Caught in the source instead (scripts/check_sidebar_scan
    # .py), which is why this defect's gate is source-checks and not the gate.
    "sidebar.raw_rescan": dict(
        gate="source-checks", build="none",
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
    # The case-insensitive substring test put back the way it was spelled
    # everywhere before contains_lower: build a lowercased copy of the
    # HAYSTACK, then find() in it. One malloc per candidate per frame, which
    # both filter arms of alloc-gate read as a level that scales with the
    # catalog. Nothing else changes and no answer changes; the differential
    # arm of tests/unit/test_contains_lower.cpp stays green against it, which
    # is why the gate and the test are both here.
    "alloc.ci_copies_haystack": dict(
        gate="alloc-gate", build="app",
        patches=[(FMT, """    if (lowerNeedle.empty()) return true;
    if (lowerNeedle.size() > hay.size()) return false;
    const char first = lowerNeedle.front();""",
                  """    if (lowerNeedle.empty()) return true;
    if (lowerNeedle.size() > hay.size()) return false;
    return to_lower(std::string(hay)).find(lowerNeedle) != std::string::npos;
    const char first = lowerNeedle.front();""")]),
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
        gate="scaling-gate", build="app",
        patches=[(SB, "        return (expandedMore || total <= cap) ? total : cap;",
                  "        (void)expandedMore; (void)cap; return total;")]),
    "scaling.both": dict(
        gate="scaling-gate", build="app",
        patches=[(SB, "        return (expandedMore || total <= cap) ? total : cap;",
                  "        (void)expandedMore; (void)cap; return total;"),
                 (SB, "        if (!uniformHeight) return w;",
                  "        if (true) return w;\n        if (!uniformHeight) return w;")]),
    "scaling.virt_only": dict(
        gate="scaling-gate", build="app",
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
        patches=[(TH, "    if (const float* hit = memo.find(s, px, 0.0f)) {",
                  "    if (const float* hit = (const float*)nullptr) {")]),
    "text.memo_bound": dict(
        gate="text", build="app",
        patches=[(MP, "        constexpr std::size_t kLineCountEntries = 512;",
                  "        constexpr std::size_t kLineCountEntries = 4096;")]),
    # ---- perf_transcript_slope ------------------------------------------
    "transcript.render_cache": dict(
        gate="slope", build="app",
        patches=[(MP, "            if (const auto* hit = render_cache().get(key, textW)) {",
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
        patches=[(FV, "        ctx.theme.focus_ring_thickness = fv::ring_thickness();",
                  "        ctx.theme.focus_ring_thickness = 0.0f;")]),
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
    "sidebar-scan-gate": ["bash", "scripts/sidebar_scan_gate.sh"],
    "source-checks": ["make", "source-checks"],
    "alloc-gate": ["bash", "scripts/alloc_gate.sh"],
    "launch": ["bash", "scripts/measure_launch.sh"],
    # The screenshot subset `make test` runs. It captures and compares, so it
    # needs the built app and the machine to itself, like the UI suite.
    "shots": ["make", "validate-screenshots-fast"],
}


def run(cmd, env=None, timeout=1200):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, env=e, capture_output=True, text=True,
                       timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def build(kind):
    if kind == "none":
        return True, ""
    tgt = "uitest-build" if kind == "uitest" else "-j8"
    rc, out = run(["make", tgt] if kind == "uitest" else ["make", "-j8"])
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
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if "--list" in sys.argv:
        for k, v in DEFECTS.items():
            print(f"{k:28s} -> {v['gate']}")
        return
    names = args or list(DEFECTS)
    results = {}
    for name in names:
        d = DEFECTS[name]
        print(f"=== {name}", flush=True)
        saved = apply(d.get("patches", []))
        try:
            ok, blog = build(d["build"])
            if not ok:
                results[name] = {"build": "FAILED", "log": blog}
                print("  BUILD FAILED\n" + blog)
                continue
            rc, out = run(GATE_CMD[d["gate"]], d.get("env"))
            results[name] = {"rc": rc, "out": out}
            open(f"{OUT}/{name}.log", "w").write(out)
            print(f"  rc={rc}")
        finally:
            restore(saved)
            # Rebuild on the way out. Leaving a defective binary in output/ is
            # how the next clean run reads as a catastrophic regression.
            build(d["build"] if d["build"] != "none" else "app")
    open(f"{OUT}/results.json", "w").write(json.dumps(results, indent=1))


if __name__ == "__main__":
    main()
