# Hanabi day plan (2026-08-02) — ~50 items, executing top-down while Gabe is out

Rules: gate every commit (build/TLS/test 8/8/perf; test-real for data changes).
No vendor edits (log gaps). No company names. Mock default. One mutator per file
(subagents get isolated files; main_pane_system.h + loader_system.h are MINE).

## TIER 1 — PERF
1  [ ] T7 idle-frame skip (attempt app-side; log gap if vendor-blocked)
2  [ ] Sidebar fold/close relayout jank
3  [ ] Cache smart-view/home partition (per-frame recompute)
4  [ ] Memoize sidebar row width math
5  [ ] Frame-timing regression guard in make test

## TIER 2 — CHAT FIDELITY (main_pane, mine)
6  [ ] Inline code pills (short-line styled runs)
7  [ ] Copy button on code block
8  [ ] Fold long code blocks
9  [ ] Markdown lists (bullet/numbered hanging indent)
10 [ ] bold/italic runs
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
26 [ ] Draggable scrollbar (scrollbar.h — SUBAGENT)
27 [ ] Tab reorder (tab_bar — SUBAGENT)
28-33 [ ] keyboard nav, Cmd+W/[/], Cmd+F, Esc consistency, row context menu, tab mid-click

## TIER 5 — LIGHT THEME
34 [ ] Light-theme full sweep (theme.h — SUBAGENT)
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
