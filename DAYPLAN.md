# Hanabi day plan (2026-08-02) — ~50 items, executing top-down while Gabe is out

Rules: gate every commit (build/TLS/test 8/8/perf; test-real for data changes).
No vendor edits (log gaps). No company names. Mock default. One mutator per file
(subagents get isolated files; main_pane_system.h + loader_system.h are MINE).

## TIER 1 — PERF
1  [x] T7 — confirmed vendor-blocked (vsync swap_interval + tree-clear); gap #27 strengthened. NOT hacked.
2  [ ] Sidebar fold/close relayout jank
3  [ ] Cache smart-view/home partition (per-frame recompute)
4  [ ] Memoize sidebar row width math
5  [~] Frame guard — FirstFrame<250ms gate already catches gross regressions; idle-frame render needs GPU ctx (not unit-testable). Kept existing gate.

## TIER 2 — CHAT FIDELITY (main_pane, mine)
6  [~] Inline pills — VENDOR unlock proven (gap#22 patch) + turnkey wiring documented; blocked on afterhours pointer bump
7  [x] Copy button on code block (6ad8f6a)
8  [ ] Fold long code blocks (TODO)
9  [x] Markdown bullets + horizontal rules (afe16b9)
10 [~] bold/italic — needs gap#22 (same unlock as #6)
11 [ ] Links rendered/clickable
12 [ ] Inline code in tool-row command
13 [ ] Blockquotes
14 [ ] Horizontal rule
15 [ ] Message copy-on-hover

## TIER 3 — STATE/COVER AUDIT (main_pane, mine)
16-25 [ ] empty Blocked/Review/Starred/Archived, empty folder, search no-results,
        transcript error, offline banner, auth cover, first-run welcome, streaming,
        send-failed retry

## TIER 4 — INTERACTION
26 [x] Draggable scrollbar + track-paging (ace5131)
27 [ ] Tab reorder (tab_bar — SUBAGENT)
28-33 [ ] keyboard nav, Cmd+W/[/], Cmd+F, Esc consistency, row context menu, tab mid-click

## TIER 5 — LIGHT THEME
34 [x] Light-theme contrast sweep (88378b8)
35 [ ] Follow-system appearance shim
36 [ ] No-flash theme swap

## TIER 6 — REAL-DATA/API (wire when backend lands)
37-41 [ ] waiting-on summary, backward cursor, AUTH 401 refresh, workspace folders, real tool count

## TIER 7 — POLISH DETAILS
42-48 [ ] code/tool right-edge, multiline composer, model picker, sub-agent panel,
        richer live states, icon hover, focus rings

## TIER 8 — HYGIENE
49 [ ] Split main_pane_system.h
50 [ ] README/REQUIREMENTS reconcile


## VENDOR-PATCH WORKFLOW (new — Gabe OK'd editing the afterhours submodule)
- Prototype fix in vendor/afterhours, PROVE in hanabi, capture as vendor_patches/NN-*.patch + submodule tag, revert to PINNED so main builds. Maintainer applies + bumps pointer.
- DONE: #25 (rounded-corner degenerate triangle), #22 (styled spans word-wrap).
- settings_footnote overflow (light-theme agent): RESOLVED — the earlier alignment
  unification fixed it; verified 0 layout warns in a headless settings capture.

## MOCK ASSET-SPLIT (shared css/data/ui, file://-safe) — VERIFIED DONE
- mock/{index,bluesky,messages}.html are fully externalized (no inline <style>/<script>).
- Shared base: assets/hanabi.css + data.js (THREADS/FOLDER_MEMBERS) + ui.js;
  bluesky layers v2.css/v2.js, messages layers messages.css/messages.js — deltas, not copies.
- file://-safe by construction: relative assets/ paths, NO fetch/import/localStorage/.json.
- PROVEN: headless Chrome loaded all 3 via file:// — exit 0, zero asset/CORS/local-load
  errors (only Chrome-internal noise), full render (index MVP + bluesky V2 empty-states/motion).
- HARD CONSTRAINT re-checked: 0 parent-company/internal-URL leakage in mock/ (and src/, docs/).

## PHASE H ICON SWEEP — COMPLETE
- Last raw unicode chrome glyph (tool-pile count badge's ≡) migrated to Lucide 'layers'
  sprite (atlas -> 18). 0 raw unicode chrome glyphs remain (250d9b4).
