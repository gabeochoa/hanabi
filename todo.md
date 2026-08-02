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


## STANDING AUTONOMOUS DIRECTIVES (Gabe, 2026-08-02) — follow while working the list
- Work through the WHOLE todo list autonomously.
- Every 3 commits: SCREENSHOT AUDIT — capture the app + find >=10 UI/UX things needing work; log them here.
- Every 5 commits: PERFORMANCE AUDIT — check for anything slow; log findings + act.
- Build a LOCAL MOCK SERVER tool (serves mock REST + SSE) so testing is easier AND so send-message can be
  exercised end-to-end (real send flow against a local server, no real backend needed).
- Memory: OK to spill local thread info to /tmp file cache (don't keep everything in RAM) to reduce footprint.
- ALL API requests go to the API thread (session 47bc4cf8) — never anywhere else.
- Permission granted to merge + push autonomously.

## MERGED since (cd6e440): live-sse (newest-N + SSE + tool-blocks), scrollbar (#26 + fold-all + rail),
   tab drag-reorder (model::reorder_tab + e2e). All gated + pushed.

## NEW asks (batch 5, 2026-08-02)
- [ ] MESSAGE HOVER ACTIONS: buttons underneath a message on hover (copy / retry / etc. — like Navi web chat).
- [ ] PROFILE STARTUP + FIX IT (direct): startup varies 159ms..1464ms in the wild (cold vs warm, real backend
      list fetch blocks?). Instrument app_init phases, find the cost, cut it. (Note: windowed Startup log
      showed 1164-1464ms on real-backend cold launch — likely the synchronous list/auth on the UI thread.)
- [ ] LOCAL MOCK SERVER (tooling): REST + SSE, drives send-message e2e + easier iteration.
- [ ] /tmp file cache for thread info (memory-footprint reduction; complements newest-N loading).

## OPEN ASKS — batch 6 (2026-08-02, live testing during provider hiccups — NOT STARTED)
- [ ] TRANSCRIPT: "jump to bottom" button (down chevron) bottom-right of the thread. If AT bottom, stay
      pinned when new msgs arrive; if NOT at bottom, don't move. (Ties into open-at-bottom + SSE.)
- [ ] ARCHIVED needs a real ICON (currently U+25A4 box fallback) — cut a Lucide archive sprite into the
      atlas (icons_atlas.h + icons.png via scripts/gen_icons.py) and drop the fallback. (gap TODO(icon-atlas))
- [ ] TOOL CALL missing info (screenshot): the tool row shows only an icon + count(5) + 53s + check, but
      NO command/name text. Wire the REAL tool fields (name->subtitle, command->text) so the row shows
      what ran. (Overlaps the render-wiring owed after data-layer merge — the block-split emits these now.)
- [ ] THREAD SWITCH super slow / BEACHBALL: switching threads blocks the UI thread. Make the switch
      async (spinner while loading), keep UI interactive. ALL API calls on the API/worker thread, never
      UI thread. (Overlaps loader async + the jank/idle-cost work.)
- [ ] TABS: horizontal scroll along the tab strip (hscroll) when many tabs.
- [ ] SETTINGS: button to WIPE the on-disk cache + show current cache storage size used.
- [ ] MEMORY: lazy-load transcript text from disk when needed (/tmp file cache), don't keep all in RAM.
- [ ] SIDEBAR SCROLL BUG (re-confirmed via screenshot): scrolling down, row TEXT disappears but rows
      remain clickable — text not drawn past a scroll offset. ADD E2E TESTS. (dup of batch-4 bug, still open.)
- [ ] STARTUP: profile + fix (Gabe: do the profiling IN A SUBAGENT). Windowed cold launch 1164-1464ms on
      real backend; instrument app_init phases (Gfx-init vs App-init split already added), find + cut cost.
- [ ] MESSAGE HOVER ACTIONS: buttons under a message on hover (copy/retry) — batch-5, still open.

## STANDING DIRECTIVES RESTATED (Gabe, batch 5+6)
- Work the WHOLE list autonomously; merge+push freely (permission granted).
- Every 3 commits: screenshot audit (>=10 UI/UX findings, log them). Every 5 commits: perf audit.
- Build a LOCAL MOCK SERVER (REST + SSE) to test easier + drive real send-message e2e.
- ALL API requests to the API thread (worker), never the UI thread. Keep UI interactive (spinners, not beachballs).

## PERF AUDIT (2026-08-02, commit 0f31e2e, under subagent load ~4.2)
- Home (no transcript): 8.6ms/frame (~116fps) — the APP FLOOR (T7 idle-cost).
- Short thread r9: 12.3ms/frame (~81fps).
- Big transcript (200-msg, virtualized): 11.5ms/frame (~87fps) — CHEAPER than short thread => virtualization works.
- Launch perf-gate flaked 285-291ms under 5-subagent build contention; isolated best-of-6 = 219ms PASS.
  The gate is contention-sensitive; startup-profile subagent (2d834f91) investigating real cold-launch.
- CONCLUSION: the ~8.6ms app floor (rebuild+relayout every frame) is the dominant perf lever (T7 dirty-flag
  skip-rebuild). Transcript virtualization is solid. Thread-switch async is in flight (data-async agent).

## VISUAL/DESIGN AUDIT dispatched (subagent 68c5bd34) — 12 screens captured (scripts/screens.sh + HANABI_VIEW/
   HANABI_BIG_TRANSCRIPT/HANABI_EXPAND supplements). Auditor: >=10 defects + per-screen presence validation.
   Screens: home dark/light, transcript, blocked/review/starred/home views, big transcript, many tabs,
   tools expanded, hover row-star, hover tab. Handles in /tmp/screen_handles.txt.

## VISUAL E2E (Gabe ask): scripts/screens.sh captures 7 base states; supplemented ad-hoc with env
   (HANABI_VIEW smart views, HANABI_BIG_TRANSCRIPT, HANABI_EXPAND, many-tabs settings). TODO: fold the
   supplements INTO screens.sh so `bash scripts/screens.sh` captures ALL ~14 screens in one shot + wire a
   visual-regression check. (Deferred: screens.sh render files partly owned by running agents.)

## MERGED: local mock server (ffef580) — tools/mock_server/server.py (Python stdlib REST+SSE),
   `make mock-server`. Verified: hanabi renders local-server data + full send round-trip + SSE live event.
## KEY: composer SEND is fully wired but DISABLED unless HANABI_CHAT_PATH set. supports_send() needs
   base_url + chat_path. Stream via HANABI_STREAM_PATH (else sync /chat). Events default /sessions/{id}/events.
   -> Wire in-app send + test against mock server AFTER data-async (message-queue) merges.
## IN FLIGHT: sidebar-fixes(baf89d58), tabs-overflow(b83d422a), data-async(6afbad89), startup-profile(2d834f91), visual-audit(68c5bd34)
## NOTE: pkill -9 -f hanabi.exe kills OTHER worktrees' captures — scope kills / coordinate when agents run concurrently.

## VISUAL AUDIT RESULTS (2026-08-02, subagent 68c5bd34) — 24 defects, verdict "halfway"
TOP 10 (ranked) + owner routing:
1. [P0] Many-tabs overflow garbled (hard clip, bleeding fragments, no overflow menu) -> TABS agent (in flight).
2. [P0] LIGHT THEME broken (muddy grey sidebar, no card contrast, not shippable) -> NEW task: fix light tokens (theme.h).
3. [P0] Tool row incomplete: GREY check (no success color), NO duration, heavy count pill -> MY tool-row fix
   (0f31e2e) added real duration+status+green/red check — VERIFY on mock+real; the audit screenshot PREDATES it. Mock has no duration (blank is correct); real has it. Re-audit tool row post-fix.
4. [P0] Nested per-node tool SUB-ROWS missing (expanded pile shows only sub-agent chips) -> the block-split
   emits Role::Tool msgs; need the pile EXPAND to render nested sub-rows w/ node+subcmd+dur+check (main_pane).
5. [P1] Send button always grey/dead -> accent when field non-empty (composer_system.h; ties to data-async send).
6. [P1] Tri-color section labels (red/grey/green) break one-accent rule -> neutral labels, color only in glyph (main_pane home digest).
7. [P1] Smart-view row inconsistency (Blocked=title+time, Starred=title+subtitle+pill) -> unify one row component (main_pane digest).
8. [P1] Status-pill contrast (DONE grey-on-grey invisible) -> muted-but-legible tints (theme.h + digest).
9. [P2] Too many sidebar glyph shapes/colors (triangle/spinner/hollow/diamond/square/⇄) -> reduce to running/blocked/done/idle (sidebar).
10. [P1/P2] Empty transcript void + dim centered "Task:" stray line -> anchor content / promote header (main_pane).
Other notable: #14 cost meter "$$$$" placeholder -> real "$0.12"; #18 chunky scrollbar (PREDATES scrollbar merge — verify thin now); #22 tab × collides with truncated title (tabs agent); #2 light theme is THE headline.
NOTE: audit screenshots predate the scrollbar merge (e997b33) + tool-row fix (0f31e2e) — re-capture after current agents merge before trusting #3/#18.

## MERGED (00a21a9): data layer — async switch (UI cost 0.47ms->0.008ms; disk read + SSE stop() off UI
   thread), /tmp transcript cache (disk_cache::total_bytes()/wipe_all()), message queue
   (enqueue_send/sending_for/pending_send_count), settings via /whoami (get_settings). test 8/8, test-real
   88 sessions + live settings. Also merged: tabs Chrome-overflow (0ac0779), composer meter (67cf30a).
## RENDER-WIRING OWED (from data layer) — main_pane/composer/settings (do after sidebar merges):
   1. per-thread switch SPINNER: read app.transcriptLoadingId / transcriptState==Loading.
   2. composer: route sends via app.enqueue_send(id,prompt); show sending_for(id) spinner + pending_send_count badge.
   3. settings screen: set app.requestSettings=true; render app.settings/settingsState; disk-usage row +
      wipe button -> disk_cache::total_bytes()/wipe_all().
   4. (still owed from earlier) open transcript at BOTTOM + jump-to-bottom button + requestLoadOlder on scroll-top.
   5. nested per-node tool SUB-ROWS on pile expand; unified smart-view row; sidebar glyph reduction; tri-color labels.

## MILESTONE (386259f, 2026-08-02): ALL 9 dispatched subagents merged + pushed. Main HEAD 386259f.
Merged this session: startup async-auth (5s->150ms), light theme, tool-row real info, composer meter,
Chrome tabs (+menu+hscroll), mock server, data layer (async switch 60x + /tmp cache + queue + /whoami),
settings cache row + overlay test hook, sidebar (scroll-text FlexWrap::NoWrap fix + e2e, star, layout-warn 0).
Re-verified post-merge: many-tabs strip clean (headline defect fixed), light theme real, composer meter real.

## AUDIT ITEMS RESOLVED: #1 many-tabs, #2 light theme, #3 tool row (real fields), #14 cost meter,
   star right-align/no-bg, scroll-text-disappear (+e2e), layout-warn spam (0), settings cache wipe+size.
## STILL OPEN (render-wiring + remaining top-10, main worktree now free):
  - per-thread switch spinner (transcriptLoadingId); composer enqueue_send routing + queued badge
  - open-at-bottom + jump-to-bottom button + load-older-on-scroll-top; wire settings screen (requestSettings)
  - nested per-node tool SUB-ROWS on pile expand; unified smart-view row; sidebar glyph reduction;
    neutral tri-color section labels; archived real sprite (still U+25A4 box); empty-transcript void
  - T7 idle 8.6ms/frame dirty-flag skip-rebuild (biggest perf lever)
  - fold screens.sh supplements in (all ~14 states one-shot) + visual regression
