# Hanabi — TODO

Native macOS Navi client: C++23 + afterhours (ECS/UI) + Sokol/Metal, Make build,
mock backend default (real behind config/env, never hardcoded). Repo: gabeochoa/hanabi.

This file is the SOURCE OF TRUTH for everything Gabe has asked for. Grouped by
status. Dev-in-mock, `make test` + `make test-real` before every push, one
repo-mutator per file (parallel agents in isolated worktrees, parent merges gated).

---

## HARD CONSTRAINTS (never violate)
- [x] Clean git history, pushed to gabeochoa/hanabi (private).
- [x] NO parent-company mentions anywhere in the repo.
- [x] Real API never hardcoded — generic HTTP/SSE adapter behind the mock interface; mock is zero-config default.
- [x] NEVER edit vendor/afterhours — log gaps to afterhours_gaps.md instead.
- [x] No `git add -A` — stage only owned files per agent.

---

## DONE + MERGED + PUSHED (this session, newest first)
- [x] Live SSE + memory-light newest-N + tool-call block-splitting (wt/live-sse be636ed — PENDING MERGE).
- [x] icons.h TODO(icon-atlas) convention for missing-sprite fallbacks (372c6e1).
- [x] Archived moved from a folder into the Views section (160f481). Unarchive-on-send already worked.
- [x] Real API folders (dynamic from session `workspace` field), no hardcoded folders, no day-grouping (30414a2).
- [x] Dropped invented "Recent" folder — unfoldered sessions render headerless (552fcf1).
- [x] Star/time swap in sidebar rows — star left of time (dfd33aa).
- [x] Uniform tab widths + transcript text fills pane width (217819f).
- [x] Clean user-bubble corners (roundness 0.5->0.12) + intra-message line culling (e12a97a).
- [x] Scroll direction respects macOS natural-scroll pref; tab-corner triangle fix (gap #25); transcript text clip + bottom pad (9d351c3).
- [x] Compile speed: parallel-default + ccache (d69b156). Chat overhaul: virtualize+memoize 145ms->16ms/frame + dense doc-feed (c088ebd).
- [x] afterhours WISHLIST added (0122fdc). UI re-review done (44 defects, drove the overhaul).

## IN FLIGHT (subagents, isolated worktrees)
- [~] SCROLLBAR — wt/scrollbar (agent). Temp overlay scrollbar on scroll views + gap #26; fold-all
      right-aligned to full sidebar width; fold-all stale-keys fix (build from distinct_folders);
      collapsed-rail icons left-aligned; archive-sprite TODO. Committing now; then MERGE.
- [~] DATA LAYER done (wt/live-sse be636ed) — MERGE next: newest-40 on open + has_more_older +
      requestLoadOlder; live SSE (/sessions/{id}/events, debounced refetch); tool-call blocks ->
      Role::Tool msgs with real name/command/output/status/duration fields.

## MERGE QUEUE (gated: build+tls+test+test-real, then push, refresh live pin)
1. Merge wt/scrollbar (render files).
2. Merge wt/live-sse (data files) — mostly disjoint; components.h field-append overlap, resolve.
3. After both: wire the RENDER side of the data-layer features (see below).

---

## RENDER WIRING owed after data-layer merge (main_pane_system.h)
- [ ] Open transcripts at the BOTTOM (newest) by default — loader now delivers newest-40.
- [ ] Set app.requestLoadOlder=true when scrolled to TOP and app.hasMoreOlder — loader fetches full transcript (interim: no backward cursor yet).
- [ ] "load older messages" affordance/spinner keyed off hasMoreOlder / loadingOlder.
- [ ] Tool-row renderer: replace HASHED tool_duration/tool_count/status with REAL Message fields
      (tool_duration_ms, tool_status, tool_result). subtitle+text already flow. Show real output + completed/error check in nested sub-rows.

## OPEN ASKS — batch 4 (2026-08-02, live testing — NOT STARTED)
- [ ] TABS: allow REARRANGING tabs (drag to reorder).
- [ ] TABS: right-click context menu on tab titles — "Copy Navi URL" + "Close others".
- [ ] TABS: overflow handling when many tabs open — look at how Chrome does it (shrink-to-fit,
      scroll/overflow chevron, min width, active tab stays visible). Current many-tabs state is bad.
- [ ] FONT SIZING: something wrong with font sizing (see screenshot) — investigate + fix.
- [ ] SEARCH: the search-input highlight/focus ring should cover the WHOLE search input (likely an
      afterhours gap — if so log it + temp fix).
- [ ] SIDEBAR ROW STAR: star should be RIGHT-aligned and have NO background of its own (currently a
      boxed bg). (Refines the earlier star/time swap.)
- [ ] BUG: scrolling down in the sidebar list — the TEXT disappears but rows remain (clickable). Text
      not rendering past a certain scroll offset (see screenshot). Likely a virtualization/clip/scissor
      or text-draw-culling bug. ADD E2E TESTS to catch what's causing this (Gabe: "add more e2e tests
      to check for what is causing this").
- [ ] LAYOUT WARN SPAM: log floods with `Layout wrap/overflow: 'row_time'/'row_star_slot'/'row_title'/
      'sb_actions'/'sv_count'/'folder_count' ... NoWrap would overflow` — the fixed-width row columns
      (title+star+time) overflow the row content box at narrow widths. Fix the width math (gap #18
      no-flex-grow reserves) so columns fit; silence the warn spam. (This is ALSO a perf drag — every
      overflow triggers solve_violations.)
- [ ] JANK (measured): idle frame = flat 8.6ms EVERY frame (app never idles, ~111fps, zero headroom) →
      sidebar-close animation + scroll feel janky. ROOT: afterhours rebuilds every widget + full-tree
      solve_violations layout EVERY frame unconditionally + our sidebar re-sorts distinct_folders every
      frame. FIX (T7): dirty-flag skip-rebuild when nothing changed; cache distinct_folders; kill per-
      frame allocs. Biggest perf lever. (afterhours wishlist B.)

## SCHEDULED SUBAGENT WORK (Gabe asked to schedule)
- [ ] Subagent: INPUT BOX + MESSAGE QUEUING — the composer send flow, queue messages while a
      reply/stream is in flight, ordering, retry, draft handling.
- [ ] Read SETTINGS from the API so hanabi can verify things are set up correctly (user settings/config
      surfaced by the backend). (Depends on API exposing a settings endpoint — ask maintainers.)

## API REQUESTS SENT to maintainers (session 47bc4cf8; PR par-msl/navi#4081 MERGED — deploying)
- [x] #4081 merged: expected to deliver folder/pagination. WATCH: verify real folders appear on deploy.
- [ ] Requested: per-session workspaceId on /api/v1/sessions; ?workspaceId filter honored; cursor
      pagination (237 sessions, only ~88/page today, offset ignored); message backward-cursor
      (/messages offset/before currently ignored).
- [ ] Requested: AI "waiting on" summary + attentionState per session (Gabe's idea) — server-side beats
      client heuristics for the Blocked view. Wire into render_digest Blocked view when it lands.
- [ ] TODO ask: a settings/config read endpoint (for the "read settings from API" item above).

## afterhours GAPS logged this session
- #22 wrapping styled labels; #23 HasScrollView virtualization hook; #24 hard-\n in wrapped text;
  #25 mixed round/sharp corner degenerate triangle; #26 no built-in scrollbar widget.
- Plus a WISHLIST section (text-measure API, per-frame memo/dirty layer, alpha fills, animation kit,
  platform shim, headless/test harness, flex-grow, mono font tier, sprite-atlas, scroll_to, hover-read).
- Candidate NEW gaps to log: search-input focus-ring can't cover full input (verify); (jank) no
  dirty/skip-rebuild so idle frames cost full relayout.

## KEY API FACTS (live-verified 2026-08-02, CLI Bearer token, /api/v1)
- /sessions ?limit=N + hasMore; 237 total; offset ignored. workspace=null on all rows today.
- /sessions/{id}/messages ?limit=N = newest N ascending + hasMore; offset/before ignored.
- /sessions/{id}/events = LIVE SSE (data:{type:connected} then {sessionId,event:{type},ts}; ignore context_usage).
- Real messages: roles user/assistant only; tool calls = tool_call/tool_result BLOCKS in assistant msgs
  (tool_call{id,name,inputs,startedAt}, tool_result{output,status,toolCallId,completedAt}).
- Folders: only /api/v1/workspaces (1: "Whole foods"); /api/folders + /api/workspaces = 401 for CLI token (cookie auth).

---

## NON-FUNCTIONAL BUDGETS (gates)
- Startup/FirstFrame < 250ms (currently ~140-230ms). Peak RSS < 250MB (~51MB). Thread-switch cached < 1ms.
- Perf-gate metric = screenshot-harness FirstFrame (note: measures harness async-wait, not pure launch — T6 make honest).

## STANDING WORKFLOW
- Dev on mock; `make test` (unit+e2e+perf) + `make test-real` (read-only real smoke) before EVERY push.
- Screenshots only on aspen/boulder. Merge -> rebuild main -> screenshot -> pin ONLY at real milestones.
- One repo-mutator per file; subagent "(no output)" != done (verify via git/check_agent).
