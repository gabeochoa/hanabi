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

## MESSAGE ACTIONS + TOOL PRESENTATION — COMPLETE (2026-08-27)
- [x] Hover/focus overlay is zero-height and built only while visible; copy feedback is isolated by pane and thread.
- [x] Copy preserves source bytes; retry appears only on eligible user prompts and enters the persistent outbox before dispatch.
- [x] Tool summaries show real name/node/command, centered count, and right-aligned duration/status; expansion preserves calls and output.
- [x] Pointer, keyboard, clipboard, retry persistence, split-pane focus, long/no-output/failed tools, fold persistence, event classes and nested subagents are covered.
- [x] Busy event transcript: 15/240 turns = 2345/2381 allocations/frame, slope 0.16/turn, under 2900 and 2.0 gates. Remaining pass-1 cost is #455.

## HISTORICAL MERGE STATUS (reconciled 2026-08-27)
- [x] Live SSE + memory-light newest-N + tool-call block-splitting (`be636ed`) merged.
- [x] Scrollbar, fold-all, collapsed rail and archive sprite merged (`e997b33`, `0c99ec7`).
- [x] Data-layer render wiring merged: newest-first bottom follow and jump control
      (`3316179`, `5ae33ee`), load-older feedback, and real tool fields (`734b4c4`).
- [x] Archived is a smart view with a real Lucide archive sprite; sending to an
      archived thread still unarchives it.
- [x] Real API folders, headerless unfoldered sessions, sidebar star/time order,
      uniform tabs, transcript width, bubble corners, natural scrolling, build
      caching and transcript virtualization all remain merged.

There is no active merge queue represented in this document. Old worktree names
and merge ordering were removed because they were historical state presented as
current instructions.

---
## OPEN ASKS — batch 4 (2026-08-02, live testing — NOT STARTED)
- [x] TABS: drag reorder was already implemented in `tab_bar_system.h` + `model::reorder_tab`.
- [x] TABS: the right-click menu already existed; this pass corrected the label to "Copy Navi URL",
      preserves pinned tabs in Close others, and exercises the real clipboard action.
- [x] TABS: Chrome-style shrink/overflow and active visibility already existed; this pass added native
      horizontal trackpad deltas, retained vertical-wheel/Shift behavior, and covered narrow overflow.
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
- [x] TRANSCRIPT: jump-to-bottom button and follow latch (`3316179`, `5ae33ee`, merge `cb47404`).
      It appears only after leaving the bottom, a click returns to the newest message, and the latch
      re-arms when the scroll reaches the end. Split-pane behavior is covered separately.
- [x] ARCHIVED uses the real Lucide `archive` sprite (`0c99ec7`); the generated atlas, map and header
      all carry it. The U+25A4 value remains only as the generic atlas-load fallback path.
- [x] TOOL CALL detail: collapsed rows show real name, node, command, duration and status; expansion preserves each call and output (`feat/message-actions`).
- [ ] THREAD SWITCH super slow / BEACHBALL: switching threads blocks the UI thread. Make the switch
      async (spinner while loading), keep UI interactive. ALL API calls on the API/worker thread, never
      UI thread. (Overlaps loader async + the jank/idle-cost work.)
- [x] TABS: horizontal tab-strip scrolling was already built; this pass added native horizontal
      trackpad deltas and kept vertical-wheel-over-strip plus Shift+wheel semantics.
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
## HISTORICAL BRANCH SNAPSHOT: sidebar fixes, tab overflow, async data, startup profiling and the visual audit all merged by milestone `386259f`; this line is not an active ownership or merge queue.
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
## HISTORICAL RENDER WIRING — COMPLETE OR REASSIGNED
   1. [x] Per-thread switch spinner reads pane-local loading state.
   2. [x] Composer sends through the queue and exposes pending state.
   3. [x] Settings reads server state and exposes cache size/wipe.
   4. [x] Newest-first bottom follow, jump-to-bottom and load-older feedback are live (`3316179`, `5ae33ee`).
   5. Tool-message sub-row/footer work belongs to the message-actions branch. This UI-polish branch
      does not touch that rendering; its remaining owned work is smart rows, sidebar status vocabulary,
      neutral Home labels and the empty transcript state.

## MILESTONE (386259f, 2026-08-02): ALL 9 dispatched subagents merged + pushed. Main HEAD 386259f.
Merged this session: startup async-auth (5s->150ms), light theme, tool-row real info, composer meter,
Chrome tabs (+menu+hscroll), mock server, data layer (async switch 60x + /tmp cache + queue + /whoami),
settings cache row + overlay test hook, sidebar (scroll-text FlexWrap::NoWrap fix + e2e, star, layout-warn 0).
Re-verified post-merge: many-tabs strip clean (headline defect fixed), light theme real, composer meter real.

## AUDIT ITEMS RESOLVED: #1 many-tabs, #2 light theme, #3 tool row (real fields), #14 cost meter,
   star right-align/no-bg, scroll-text-disappear (+e2e), layout-warn spam (0), settings cache wipe+size.
## UI POLISH STATUS (reconciled 2026-08-27)
  - [x] Jump-to-bottom now focuses and explicitly re-arms its own pane (`ad82541`).
  - [x] Archived uses the Lucide sprite with no box fallback (`e8212d9`).
  - [x] All four smart views use the same title, status-pill and metadata row (`92278a6`).
  - [x] Home section labels are neutral; state color remains on the disclosure glyph (`1cc1b51`).
  - [x] Sidebar rows use running/blocked/done/idle only, with unchanged 811/1163 home
        allocations per frame and 322/428 widgets at 20/2000 sessions (`c2b9286`).
  - [x] Empty transcripts use a two-line state anchored above the composer (`e5b52a5`).
  - [x] Status pills were already fixed (`8a0db3f`, `fe22503`): measured foreground/background
        contrast is 4.92–6.21:1 dark and 4.71–6.01:1 light.
  - Message-action/tool footer and nested tool sub-rows are owned by the message-actions branch;
    this branch does not touch that rendering.
  - T7 dirty/skip-rebuild work remains the largest unrelated performance lever.

## PHASE AUTH — COMPLETE (d80701d). Device-code flow was already live-proven (no client_id/secret —
   client mints its own deviceCode; endpoints /api/cli/auth/code+poll verified live). Startup async fix
   made begin() non-blocking. NOW ADDED token-refresh: DeviceCodeFlow::refresh() -> POST
   /api/cli/auth/refresh {refreshToken} (endpoint verified real: 401=exists/needs-token). Captures +
   rotates refresh_token; config+env driven; 2 unit tests. Phase AUTH has no remaining gaps.
## PHASE G — menu-bar NSStatusItem already shipped. Native extras dispatched (agent e70ea435, wt/phase-g):
   global hotkey (Cmd+Shift+N), native notifications on blocked-count increase, Spotlight (best-effort;
   likely needs .app bundle — honest verdict expected). Follows the extern "C" + poll-take-flag seam.

## PHASE G — COMPLETE (merge fa56957; packaging completed on `feat/native-bundle`). Native extras:
   - Focus-gated Carbon hotkeys for New Task and the command palette.
   - Per-thread blocked/finished notifications with mute, quiet-hours, 30-second debounce,
     configurable sound, and click-to-open; delivery now uses `UNUserNotificationCenter`.
   - CoreSpotlight catalog sync is real in the bundle and remains a safe no-op in the bare
     developer executable. URL-scheme deep links share the notification click route.
   Headless screenshot/script paths install none of these process or system integrations.
## NATIVE FOLLOW-UP — COMPLETE (`feat/native-bundle`): `make app` now produces a self-contained,
   ad-hoc-signed `Hanabi.app` with stable bundle id `io.github.gabeochoa.hanabi`; explicit reversible
   install/register targets; `UNUserNotificationCenter` authorization/delivery; and a bounded
   CoreSpotlight catalog with title, preview, deep-link URL, update, and deletion. Bare `make run`
   and every headless path keep zero native side effects. See `docs/macos-bundle.md`.

---

## 🔴 GABE FEEDBACK — 2026-08-03 (batch, HIGH HEAT — he feels ignored; fix + verify each)
Ranked by heat. Each must be VERIFIED (screenshot / e2e), not assumed.

1. [x] **AUTOSCROLL: transcript does NOT stay pinned to bottom** (REPEATED complaint — top priority).
       New messages / streaming should keep the view at the bottom; it drifts up one message.
       Root cause candidate: stale content_size race — pin clamps against LAST frame's height,
       then atBottom recomputes false after growth so following stops. Fix = a persistent
       follow-latch broken only by a real user scroll-up (not by the geometry growth race).
2. [x] **Corners too round** on the tool-call card (screenshot). Reduce roundness.
3. [x] **User prompt bubble corners MUST MATCH the tool-call card corners** (same roundness).
4. [x] **Tool-call ROW header left-align**: "🔑 2 tool calls · [cli:aspen] cd …" should be left aligned.
5. [ ] **Tool-call footer meta** ("⚙ 2 … 5s ●"): count centered, duration+status-dot right aligned.
6. [x] **Remove the message-count from the transcript header** — don't need it at top; put it in the
       TAB TITLE instead (we don't really care how many messages).
7. [x] **Settings: make it WIDER so you don't need to scroll.**
8. [x] **Settings: buttons must ACTUALLY work + DO something** — every control wired.
9. [x] **Settings: add E2E tests** proving each control does something.
10. [x] **Settings persistence + sync**: save locally AND periodically sync so web always matches local.
11. [x] **"Coming soon" needs a better UI** (the stubbed settings rows look bad).
12. [x] **Notifications + sound: do they ACTUALLY work?** Verify Phase G native notifications fire;
        add sound if missing. (Gabe asked point-blank — needs a real yes/no + proof.)
13. [x] **Panel snapping support was never added** (I2 split-view / snap a tab L/R). Still owed.
14. [x] **Scrolling still too chunky** — smooth-scroll not active (needs vendor patch #30 landed OR a
        hanabi-side wheel-smoothing fallback that works against pinned edfe234).
15. [ ] **"you still didnt fix this"** (screenshot) — RE-IDENTIFY exact item on next relaunch; likely
        one of the above resurfacing. Ask/confirm which.

> NOTIFY-VERIFIED (`feat/native-bundle`): notifications + sound use
> `UNUserNotificationCenter`. The bundled app requests permission on its first windowed launch;
> a queued first event is delivered after authorization. The caller preserves per-thread transitions,
> mute/quiet-hours gates, 30-second debounce, sound choice, and click-to-open. Bare/headless runs are no-ops.

> BATCH-2-STATUS (2026-08-03): #4 tool header now shows '[node] cmd' left-aligned (9304ef9).
> #7-11 settings reworked (wider 2-col, all controls wired+persisted, periodic server sync,
> coming-soon redesign, +test_settings 9/9) merged from wt/settings (7d4489d). #13 split-view
> shipped (d91a503): right pane via HANABI_SPLIT + right-click tab 'Open in split'; per-session
> scroll statics so panes don't clobber each other. #14 hanabi-side eased scrolling (837e82e).
> OWNED ELSEWHERE: #5 tool footer is assigned to the message-actions branch. This branch deliberately
> does not touch tool-message rendering or resolve that layout choice.

## GABE FEEDBACK 2026-08-03 (batch 3) — all done
- [x] Padding between star and time (star drawn 13px in from slot right edge) — 3c56657
- [x] ENTER sends the message (was click-only; text_input had no on_submit) — babfb7a
- [x] Center the green status dot in the tool-meta cluster — d3c6370
- [x] Group the tool-meta icons (count/dur/dot) tighter — d3c6370
- [x] Render markdown pipe-tables as a real grid — d3c6370
- [x] Search input hover + focus-ring state — d3c6370
- [x] Turn off noisy hotkey/menubar logging (HANABI_NATIVE_LOG gate) — d3c6370
- [x] Window close hangs/freezes → app_cleanup persists then _Exit(0) (skip blocking future dtors) — d3c6370

---

## BRANCH feat/desktop-parity-polish (PR #2) — 2026-08-22

Transcript polish + a real UI test harness. `make test` green throughout:
11/11 unit+e2e, 11/11 scripted UI, launch 223ms / RSS 47MB, build 0/0.

### Shipped
- **-O2.** There was no `-O` flag in the main build — only the perf
  micro-benchmark asked for one, so every binary including `make app`'s bundle
  was unoptimised. Home idle 5.45ms -> 0.95ms, short transcript 6.25 -> 1.14,
  120-message transcript 9.08 -> 1.64. **The "8.6ms idle-frame floor" on this
  list was 5-6x compiler flag, not afterhours.** `make OPT=0` for the old build.
- **Short threads bottom-anchor.** They were floated mid-pane by a leading
  spacer of 1/3 the slack — ~170px of void above and ~290px below, and every
  new message shifted the thread up by a third of its height.
- **Hover a message to Copy it.** There is no text selection anywhere in the
  transcript, so this is the only way text leaves the app. Code blocks' Copy
  gained the same in-place "Copied" confirmation.
- **Enter replies instead of starting a new thread.** The submit listener is
  attached with `addComponentIfMissing`, so it captured `kickoff` from a frame
  before the session had loaded and kept it forever; Send (recomputed per frame)
  was right and Enter was wrong. Listener decides nothing now.
- **A new thread's tab shows the thread, not `new1`.**
- **Characters the font cannot draw.** Roboto has no Arrows / Geometric Shapes /
  Box Drawing block and a missing codepoint draws NOTHING — no tofu. So the
  composer hint read a bare "send", the Send button had a trailing gap, the
  sub-agent rollup and tool piles had no disclosure triangle, `---` drew a blank
  line, and a streaming reply had no caret. Found by reading the font cmap
  against every non-ASCII literal in src/. Shaped ones are vectors now.
- **One hairline focus ring.** It was drawn as concentric rounded rects sharing
  a roundness FRACTION, so each ring's corner radius grew — bracket marks on
  every corner of the composer.
- **Split-view composer takes the left pane's width** (it replies to the primary
  thread; full-width gave no clue which one you were typing into).
- **Sidebar row hover** uses `mouse_in_subtree` — drops the star-id map.

### New harness
- `make uitest` — a second binary with afterhours' e2e input hooks, driving the
  real widget tree from `tests/ui/*.e2e`. 11 scripts, each verified to fail
  against a build without its fix. Folded into `make test`.
- `scripts/screens.sh` captures 29 screens, not 7 — every smart view, the folded
  rail, all three overlays, ten tabs, split, and the transient states.

### afterhours gaps filed: #37-#48
Text selection (#37, the big one — the geometry is already in their
`text_selection.h` and only `text_input` can reach it), container hover (#38),
three ways the e2e harness reports unearned success (#39/#40/#47 — all three
bit us in the first hour), no worked host-loop example (#41), per-frame text
re-measurement bypassing their own `TextMeasureCache` (#42, ~21% of a frame),
`dynamic_cast`-to-`strcmp` component lookup (#43, ~16%), `ComponentConfig`
copies (#44), frozen widget callbacks (#45), focus-ring corner fan (#46),
silent missing glyphs (#48). **#29 is resolved** by `mouse_was_in_subtree`, and
#27's frame-time number is retracted with an apology.

### Not done / notes
- Two faint arcs remain at the composer's left corners under focus — something
  emits a focus-coloured rounded rect at the input's content rect; unreachable
  from app code (#46).
- The context meter draws a hardcoded 38% fill while refusing to print a fake
  percentage. Pick one: wire the real number or drop the bar.
  **Decided 2026-08-22: wire the real number — see below.**

---

## COME BACK TO: the context meter has a real denominator (2026-08-22)

PR #3 removes the fake 38% bar and prints `~4.2k tokens` from a chars/4
estimate, on the stated grounds that "no backend here reports a context
window". **That premise is wrong — Gabe confirms the backend does report one.**

So the bar comes back, with a true percentage behind it. What we already know
from hanabi's own source:

- The backend emits a **`context_usage`** SSE event and hanabi deliberately
  **throws it away**. `Config::event_type_ignore = "context_usage"`
  (`src/api/client.h:118`), described as a "pure telemetry kind that fires
  constantly and must NOT trigger a refetch". The filter is in
  `parse_events_frame` (`src/api/http_client.cpp:1298`, documented at
  `src/api/http_client.h:161`).
- Note the filter's *purpose* is only to stop a transcript re-fetch. Dropping
  the frame's payload as well is incidental — the event can be consumed for its
  numbers without ever triggering a refetch. That is the whole fix, if the
  payload carries a maximum.

### What the reference client does (verified in its source, 2026-08-22)

**A denominator exists — but it is two numbers, not one.** The session state
carries, on the greeting projection at attach:

    tokens.context   = { budget = 800000; window = 1000000 }
    tokens.occupancy = { tokens = 258937; basis = settled; stale = 0;
                         anchor_seq = 1329894 }

- `window` — the model's actual context window.
- `budget` — what the session is allowed **before compaction**. Not the same
  number, and in the example not even close: 800k against a 1M window.

**Use `budget`, not `window`.** The reference client computes its fraction
against the budget, on the reasoning that the budget is the thing with a
consequence — hitting it triggers compaction. `window` is trivia; the user
wants to know when their thread gets summarised out from under them. (A
subagent recommended `window` and explicitly warned off `budget`; that is
backwards, and the reference implementation is the counter-evidence.)

- **Numerator:** `tokens.occupancy.tokens`.
- **Counts are real**, from the provider's own usage fields (input, output,
  cache_read, cache_creation) — not a chars/4 estimate.
- **`cache_read` and `cache_creation` are subsets of `input`**, never added to
  it. If we ever break the bar into segments, they nest inside input.
- **`occupancy.stale`** (0/1) means the reading predates content the server has
  not accounted for. Render it — a stale number presented as live is the same
  dishonesty the 38% bar was.
- **Usage accumulates across compaction**, it does not reset. `compaction_started`
  and `compacted` bookend the summarisation if we want to show it.

**Do not subscribe to the per-delta usage event.** It fires per model call
delta with no server-side throttle, which is exactly why the current adapter
filters it. Read the numbers off the state snapshot instead — it updates
durably on each call settle. There is no REST route for usage; the state
arrives on the existing stream.

### Still open

- [x] **Now unblocked — build it.** DONE (`feat/context-meter-denominator`).
      `hello.state.tokens` is parsed into `api::ContextUsage` on the session
      (`parse_context_usage`), the denominator is `tokens.context.budget` and
      the numerator `tokens.occupancy.tokens`, `occupancy.stale` renders beside
      the figure, and nothing subscribes to the per-delta usage event. The bar
      is back with the figure beside it; `context_budget_tokens` (config key /
      `HANABI_CONTEXT_BUDGET_TOKENS`) declares a budget for a backend that
      reports none, and with neither there is no bar. `compact_count` now gives
      millions the same one-decimal treatment as thousands.

Then: restore the bar, keep the token figure beside it, and add a config key so
the mock and any backend without a max degrade to the plain figure rather than
a fake fill. Do NOT reintroduce a constant.

## COME BACK TO: evaluate the agentcloud backend (2026-08-22)

Gabe asked whether hanabi should move to the backend the reference client uses.
Investigated 2026-08-22. **Verdict: it is a rewrite of `src/api/`, not a config
change. Worth doing on its own merits; not worth doing for the context meter.**

### Why no part of it is config-reachable

Our adapter's whole design — `Config` with `base_url` + path strings + JSON
field-name mappings — assumes REST endpoints returning arrays, plus a separate
SSE stream. That backend is **one WebSocket speaking a keyed-fold subscription
protocol**. Every row in the mapping table is a transport mismatch, not a
key-name mismatch:

| ours | theirs |
|---|---|
| `GET /sessions` → array | `list` command on a control channel |
| `GET /sessions/{id}/messages` | `attach` on a second subscription, then `page` backward from a cursor |
| `POST` kickoff/reply | `create` on control, then `input` on the attached sub |
| `SSE /sessions/{id}/events` | frames on the same socket — not a second connection |
| Bearer token, device-code flow | short-lived token minted from a local daemon |

So `events_path`, `sessions_path`, `messages_path`, `chat_path`, the auth
paths, and most `field_*` mappings simply stop existing. The `Client` interface
survives; `http_client.cpp` does not.

### The real work, in order of difficulty

1. **Keyed fold.** Their transcript is not a message list — it is state folded
   from four frame kinds (durable / value / delta / retract) onto keys. We
   append messages. This is the part that is genuinely new, and it is the part
   most likely to be underestimated.
2. **WebSocket transport** with reconnect, replacing REST + SSE.
3. **Auth**: mint a short-lived token at startup, cache it, re-mint on an auth
   failure. No stored secret and no account UI — sign-in becomes invisible.
   Nothing like our device-code flow survives.
4. **Backward paging** from a boundary cursor instead of offset GETs.

### What we would gain

- **Real tool-call structure.** Tool intent and result arrive as structured
  frames with name, inputs, output, status and timestamps. The RENDER WIRING
  section above still owes "replace HASHED tool_duration/tool_count/status with
  REAL fields" — this deletes that item rather than completing it.
- **The context meter, honestly** — see the section above, including per-child
  subagent spend.
- Send has no synchronous ack: the durable echo frame IS the acknowledgement.
  Cleaner than our optimistic-message + 30s-server-lag dance.

### What we would lose or inherit

- Session list is **poll-only** — a live-push frame is defined on the wire and
  never sent. Our sidebar freshness story changes.
- No delete verb, no server-side full-text search, no attachments.
- If the local daemon is offline we cannot mint a token, and there is no
  graceful degradation. We would inherit that.

### Confidence

- **Single-source.** `~/w/puffin` is a dangling symlink into a checkout that is
  not present (0 files), so only the one reference client was actually read.
  Anything attributed to "puffin" is unverified.
- The effort guess that came back was ~600 lines / 2-3 weeks. Treat as an order
  of magnitude, not an estimate — the keyed fold is the unknown.
- Frame-ordering guarantees and the error taxonomy are not documented in the
  source that was read. Both want a staging connection before committing.

### Decision

Do not switch for the context meter. If today's backend reports a maximum we
get the honest bar for a few lines; if it does not, the plain token figure is
still the honest answer. Switch only if we want the tool-call fidelity and the
protocol on their own merits — and if we do, land it as a series of small PRs
behind the existing `Client` interface, not one branch.

---

## STANDING DIRECTIVE (Gabe, 2026-08-22): PRs are too big

The #2..#7 stack is ~4,700 added lines; #2 alone is +1831/-188 over 25 files.
Too big to review. **One theme per PR from now on.**

The `-O2` finding is the case in point: a one-line build change worth a 5-6x
frame-time win, buried behind 1800 lines of unrelated transcript fixes and a
new test harness. It should have been its own PR, merged the same hour.

Rule of thumb: if the description needs a "Fixed" section AND an "Added"
section, that is two PRs. Land the small independent win first so it is not
held hostage by review of the big change. Stacking compounds this — nothing in
#3..#7 could land until #2 did.

---

## REVIEW FINDINGS: PR #3 (feat/transcript-honesty) — 2026-08-22

Reviewed, nothing blocking. Four things to come back to. **All deferred — do
not fix as part of #3.**

### [x] 1. `src/keys.h` reimplements a function afterhours already has — DONE (`c807bd1`)

The new shim is:

```cpp
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    return afterhours::testing::platform_input::is_key_pressed(key);
#else
    return afterhours::graphics::is_key_pressed(key);
#endif
```

`afterhours::input::is_key_pressed` is that exact `#ifdef`, already written —
`vendor/afterhours/src/plugins/input_system.h:636`. And `platform_test_input.h`
says so in its own header: *"This replaces the need for per-project
backend-specific wrappers."*

The eight key codes are a second copy too: `afterhours::keys::LEFT_SUPER = 343`
and friends already live in `vendor/afterhours/src/core/key_codes.h:98`. Two
copies of the same numbers can drift.

**Fix:** call `afterhours::input::*` with `afterhours::keys::*` names. The
`#ifdef` disappears and the shipping build runs the SAME path as the test build
instead of a parallel one. (Same fix already applied in floatinghotel for the
same gap.) Still an improvement over the five sites of bare `343`/`347` it
replaced — this is one rung further, not a correction.

### [x] 2. Gap #47 is fixed upstream and is weakening our own test — DONE (`c807bd1`)

`tests/ui/shortcuts_sheet.e2e` carries `# NOTE: expect_no_text takes a BARE
single word — see afterhours_gaps.md #47` and settles for
`expect_no_text Keyboard`.

At afterhours HEAD `expect_no_text` is in the `parse_quoted()` chain
(`runner.h:163`). On a bumped pin that assertion becomes
`expect_no_text "Keyboard shortcuts"` — a real assertion instead of a one-word
proxy — and the caveat comment comes out.

That is **all three** of #39/#40/#47 fixed upstream. Bump, then tighten.

### [ ] 3. The shortcuts test cannot test the shortcut — PARKED

It opens the sheet with `HANABI_TEST_OVERLAY=shortcuts` because Cmd+/ is
unscriptable (#49), so the headline binding of a feature whose whole purpose is
discovering bindings is uncovered. Honestly flagged in the script.

Nothing to do app-side until #49 is fixed in afterhours. Parked deliberately;
when #49 lands, replace the env knob with a real `key CMD+SLASH`.

### [x] 4. Esc had four owners — FIXED (`fix/esc-one-owner`)

**The premise below is stale and the conclusion was still right.** At pin
`428047e` the injector is explicitly multi-reader: `consume_press` keeps a
press readable for the whole frame and decrements in `reset_frame`, so there
is no test/prod divergence. What there WAS, in both builds, is four systems
acting on one keystroke — dismissing the find bar also dropped the transcript
selection and wiped the composer draft. `src/ecs/escape_system.h` reads the key
once and resolves it by what is on top (rename modal > modal composer >
shortcuts > settings > find > transcript; auth is deliberately absent, login
gates the app). Two agents confirmed the multi-reader behaviour independently.

Found on the way: closing the find bar drops the composer's focus, so the next
Esc does nothing until you click back in. Small, separate, still open.

### [x] 4 (original text). Under the injector, only the FIRST system to poll a key sees it

`test_input::is_key_pressed` opens with `input_injector::consume_press(key)`.
Production's `graphics::is_key_pressed` does not consume. We now have four Esc
readers, and `MainPaneSystem` (registered `main.cpp:289`) runs before
`SettingsSystem` (292), `ShortcutsSystem` (293) and `ComposerSystem` (294):

> Focus the reply field, Cmd+`,`, Esc → in the uitest binary MainPane clears
> the draft and settings stays OPEN; in the shipping binary settings closes.

Each reader is guarded by its own overlay flag so the window is narrow, but
routing these reads through the injector is what introduces the divergence, and
a test that passes on one path and not the other is the bad kind of green.

**Fix direction:** one place decides what Esc means this frame (highest overlay
wins) instead of four systems racing to read the same key.

### [x] Minor: `compact_count` loses precision exactly where it matters — DONE (`74ddf95`)

`4.2k` but `1M` for 1,500,000 — a decimal below 10 in thousands, never in
millions. If the context meter lands with a 1M window, every reading from 1.0M
to 1.9M renders as `1M`.

---

## AGENTCLOUD — next up (2026-08-22)

Landed: transport + auth, `list_sessions`, transcripts via attach/page/fold,
and **sending**. `make run` defaults to agentcloud. The composer is live — the
read-only caption is gone for this backend.

### [x] Sending (`input`) — done (`6e19b24`, streaming fixed in `5680962`)

Implemented as `send_message_streaming` + `supports_send/steer/stream`, because
this protocol has **no ack**: the durable `user_input` frame IS the
acknowledgement, and `loader_system` appends `send_message`'s return value as
the assistant reply, so a blocking version would sit through a whole agent
turn. `steer()` is the same `input` with `apply: "interrupt"` — no second
endpoint.

The part that needed a live test to get right: live text arrives BOTH ways.
`block_delta{delta:"append"}` is a true increment; a `value` frame and the
settled `durable block` at the end each carry the WHOLE block. The first
version read only the settled block, so a 65-character reply arrived as one
delta at the end — correct output, no streaming, and no fixture could catch it
because the fixtures encoded the same wrong assumption as the code. Verified
against production: 7 deltas for that reply, concatenated deltas reconstruct
the final text exactly.

### [ ] Spaces — BLOCKED, and worth knowing why

Chosen as the replacement for folder grouping. Then measured, and it cannot
work yet:

- The viewer's Spaces read WORKS. GraphQL POST to the web app host (NOT the
  orchestrator — different service, different deployment, and a different
  credential verifier), the token as a cookie, body `{"query_text": …}`.
  Verified: HTTP 200, **8 real Spaces** returned. Response nests under
  `result.data`, not the usual top-level `data`. `emoji` is null on 7 of the 8.
- **Every one of those 8 Spaces returns ZERO sessions.** Queried each with
  `xfb_agentcloud_session_list_for_viewer`; all 8 came back `0 sessions,
  next_cursor=null`.
- And the roster cannot supply the link either: the fleet/list row carries no
  space and no container at all — confirmed against 2066 live rows, the field
  set is fixed and has neither. The reference client says the same in as many
  words, and that grouping the roster by Space "can only show the viewer's
  slice of one".

So wiring it today buys 8 empty headings and files none of the 2066 sessions.
**Revisit when sessions actually carry a Space.** Worth having anyway when it
lands: the catalog row carries `created_at` / `latest_activity_at`, the real
timestamps the orchestrator's session list does not have.

Meanwhile the sidebar still groups by `workspace` minus the per-session scratch
dirs — 4 real groups, everything else ungrouped.

### [x] Don't show the welcome screen while loading — DONE (`567dcfa`)

The spinner it now shows already existed for thread SWITCHES; it was simply
unreachable, because the `!openSession` branch returned first. Gated on
`selectedId` as well as load state, to cover the frames before the loader
flips `transcriptState`.

### [x] Bump the afterhours pin — done, all three repos agree at `428047e`

Standing rule, unchanged: **afterhours is not mine to push.** Too many projects
vendor it. Changes go to Gabe; a crash is worth flagging immediately but still
not pushing. (The one push of `428047e` was an explicit one-off he authorised,
to retract a wrong note of mine that was already public.)

### [x] Cache the chat — DONE (`9f6996d`)

The machinery was all there; `disk_cache_enabled` was gated on
`backend_label == "http"`, so on agentcloud it was simply never reached. Now
"any backend that is not the mock".

Turning it on exposed a latent bug worth more than the speedup: `has_more_older`
was not serialized, so a cached transcript round-tripped as "this is the whole
thread". Agentcloud transcripts are windowed almost by definition — 11 cached
of 2200 in the one I measured — so every cached thread would have quietly lost
its "load older" and looked like it just ended.

---

# PUFFIN PARITY BACKLOG (2026-08-23)

> # ⛔ NO AFTERHOURS CHANGES. NONE. NOT EVEN LOCAL ONES.
>
> **`vendor/afterhours` is read-only. Do not edit it — not to push, not to
> commit, not even to try something.** Two separate reasons, both sufficient:
>
> 1. Roughly twenty projects vendor this library. A change that looks local
>    lands in all of them on their next bump.
> 2. **A dirty submodule working tree makes pulling updates a mess.** Even an
>    uncommitted experiment there turns the next `git pull` into a conflict to
>    untangle. Leave it pristine.
>
> **The workflow when the library is in your way:**
>
> 1. Write the gap into `afterhours_gaps.md` — what you tried, what happened,
>    the workaround and its cost, and the smallest upstream change that would
>    fix it. That file is the channel; it is how every library fix this year
>    started.
> 2. Then solve it **in hanabi**, however inelegantly. A workaround in our code
>    is always the right answer over a patch in theirs.
> 3. Hand the gap to Gabe. He decides whether the library changes; nobody else
>    does, and nobody else touches that directory.
>
> Every item below is tagged for this:
>
> - **`[APP]`** — buildable entirely in hanabi. This is almost everything.
> - **`[APP-WORKAROUND]`** — buildable, but only by working around a library
>   limitation. The workaround is named. Expect it to be uglier than it should
>   be, and do it anyway.
> - **`[NEEDS-AFTERHOURS]`** — **cannot be built without changing the library.
>   DO NOT START THESE.** Write the gap into `afterhours_gaps.md` and hand it to
>   Gabe. He decides whether the library changes; nobody else does.
> - **`[TEST-BLOCKED]`** — the feature is buildable, but the scripted harness
>   cannot drive it because of a library limitation. Build it, and say plainly
>   in the PR how it was verified instead.
>
> A crash or data-loss bug in the library is still worth flagging IMMEDIATELY —
> that judgement was right once already today. Flagging is not pushing.


Every item from `docs/breakdown/` in one list. Each breakdown doc has the UX
flow, the files, and how the thing gets proven — this is the index, not the
spec. Read the doc before starting.

**78 claims were examined and 22 were already shipped**, so this list is the
survivors. Still: **grep before you build.** Two false gaps got past their own
agent's check and were caught on review; both had hedged their wording.

Sizes are the estimating agent's, not measured. Treat as order-of-magnitude.

## Library limitations this backlog runs into

Every one is ALREADY written up in `afterhours_gaps.md`. **None of them is a
licence to edit the library.** They are here so that when an item feels
unreasonably hard, you can see it was expected and read what the workaround is.

| gap | what the library will not do | which items it touches |
|---|---|---|
| **#51** | Tell you where a byte range landed on screen | link auto-detection, sidebar snippet highlighting. Both use the same re-derive-the-wrap trick `find_highlight.h` already does. |
| **#49** | Hold the Super modifier, so no Cmd chord is scriptable | every `[TEST-BLOCKED]` item. Build them; verify by forcing state with a `# env:` line and say so in the PR. |
| **#50** | Route `graphics::is_key_*` through the e2e injector | already worked around in `src/keys.h` by using the input plugin instead. Keep doing that. |
| **#35** | Enumerate installed system fonts | typeface picker can only offer bundled fonts. Do not promise "pick any font". |
| **#22 follow-up** | Per-run FONT in a styled span (colour and weight only) | inline monospace inside a wrapping paragraph. Fenced code blocks are fine — they are their own rows. |

If you hit a NEW one: write it into `afterhours_gaps.md` in the same shape as
the entries above — what you tried, what happened, the workaround and its cost,
and the smallest upstream change that would fix it. Then work around it in
hanabi. Do not touch `vendor/afterhours`, not even locally.

### Things I checked that turned out NOT to be library gaps

Worth recording, because two of them were written down as blockers and were
wrong:

- **Horizontal scrolling exists** — `HasScrollView::horizontal_enabled`. The tab
  overflow item is buildable today.
- **A multiline `text_area` exists** — separate from `text_input`. A growing
  composer does not need a library change.
- **Drag support exists** — `HasDragListener`. Row and tab reordering are ours
  to build.
- **Right-click and context menus exist** — the tab context menu is ours.
- **A toast plugin exists** — the undo toast is ours.

## Ship-first (no dependencies, confirmed missing)

- [x] **Screenshot harness MVP** — chunks 1-3 (`feat/screenshot-baselines`). Makes
      every later UI change verifiable, and is the thing that turns afterhours
      shortcomings from anecdote into a countable list.
- [x] Session rename (~80) `[APP]` — `feat/session-rename`. Backend verb is
      advertised on attach.
- [x] Composer history walk, Up/Down (~90) — `feat/composer-history-walk`.
- [x] Muted sessions (~60) — `feat/session-mute`. Menu item, not a per-row
      bell: an always-reserved column taxed every row's title.
- [x] Home shelf collapse/expand (~50) `[APP]` — `feat/home-shelf-collapse`.

## Screenshot testing — `screenshot-testing.md`

- [x] 1. Repeat-capture determinism test — `make test-screenshot-determinism`
- [x] 2. Baseline directory + first three screens — `docs/screenshots/baselines/`
- [x] 3. Comparison script + `make validate-screenshots` (Pillow, ImageMagick
      fallback, per-screen thresholds in `manifest.json`)
- [x] 4. Unbaselined-screen handling
- [x] 5. Full baseline set — 30 screens
- [x] 6. CI gate — there IS no CI (checked: no `.github` anywhere in history),
      so it is `make gate` plus an installable pre-push hook, and it says so
- [x] 7. Diff artifacts on failure — `test-failures/` with the pair and a red diff

Determinism is already proven: two captures are byte-identical. It holds
because the mock seeds timestamps as `time(nullptr) - N`, so the difference
stays constant — **reseed the mock with absolute epochs and every time-showing
baseline rots within a day.** Baselines must come from the mock; the real
backend serves live production data.

## Session lifecycle — `session-lifecycle.md`

- [x] Session rename (~80) `[APP]` — shipped, with the row context menu the
      rest of this section hangs off
- [ ] Session fork, `/btw` (~70) `[APP]` — `/btw` is in the slash menu and
      reports that this client cannot fork yet
- [x] Session archive (~60) `[APP]` — `feat/session-archive-ui`, with an undo toast
- [x] Session pin / star (~50) `[APP]` — was already built; the gap was the
      undo, which `feat/undo-toast` closed
- [x] Session mute (~40) `[APP]`
- [ ] Sub-agent status panel — partial (~80) `[APP]`. The transcript rollup is
      built (`render_sub_agent_panel`); what is missing is the sidebar toggle
- [ ] ~~Delete session~~ **BLOCKED** — no server verb exists

## Composer — `composer.md`

- [x] History walk, Up/Down (~90) `[APP]`
- [x] Slash command menu `[APP]` — `feat/slash-commands`. `/new` runs; the
      rest say plainly what this client cannot do rather than reaching the agent
- [x] Model picker popover `[APP]` — the strip's chip named a model nothing set
- [x] Effort level picker `[APP]` — local-only; nothing sends it yet
- [x] Undo toast for archive/pin/mute `[APP]`
- [ ] ~~Skills chip~~ **BLOCKED** — nothing in the client or on the wire knows
      what skills a session has; `/whoami` carries one account-wide count of
      skills the USER authored. Evidence in `docs/breakdown/composer.md`
- [x] Streaming animation, working dots `[APP]` — **the correction was itself
      wrong.** `render_thinking_indicator` (pulsing dot + Thinking… + elapsed
      timer) has been there for months; what was broken was the demo knob, so
      nobody could see it — the loader replaced the injected session during the
      settle frames, and a chunkless stream was declared Done on the next tick.
      Fixed and covered in `fix/thinking-demo-capture`. Grep before you build:
      this one got past two agents by inspection of the docs, not the code.

Token meter: the bar exists but only draws with a configured context window,
which nothing sets. Real denominator is queued separately above — do not
re-plan it here.

## Transcript — `transcript.md`

- [x] 1. Date dividers `[APP]` — `feat/transcript-date-dividers`. Local-day
      boundary, not the doc's four-hour gap (a gap rule prints twice in one day
      and nothing across midnight)
- [x] 2. Thinking disclosure, collapsible `[APP]`
- [x] 3. Fold defaults for tool rows `[APP]`
- [x] 4. Message delivery status rows `[APP]` — ALREADY BUILT: `api::SyncState`
      per message + `draw_sync_check` in the transcript. False gap
- [x] 5. Syntax highlighting in code blocks `[APP]` — fenced blocks are their
      own rows, so per-line colour works. INLINE mono inside a paragraph does
      not: `TextSpan` carries colour and weight but no per-run font.
- [x] 6. Markdown H1-H4 `[APP]` — `feat/markdown-headings`
- [x] 7. Streaming animation, pulsing dots `[APP]` — see the composer entry:
      built all along, the demo knob was what was broken
- [x] 8. Link auto-detection for work-tracker ids `[APP-WORKAROUND]` — needs
      to know where a byte range landed on screen, which the library will not
      tell you (no `text_rects_for`). Same trick `find_highlight.h` already
      uses: re-derive the wrap with the library's own wrapper. Fragile, and
      already written down as a gap.
- [x] 9. Exclude thinking rows from find `[APP]`
- [x] 10. Minimap navigator `[APP]` — a custom-drawn rail via `on_draw_fg`,
      reading the same item list the transcript virtualizes from
- [x] 11. Transcript behaviour settings `[APP]`

**Any layout change here must keep `rich_body_h` and `render_rich_body` in
step** — they measure and draw the same thing, and drift desyncs the
virtualization spacers. A recent change here silently mis-sized every
multi-line bubble.

## Sidebar & tabs — `sidebar-tabs.md`

- [x] Home shelf collapse/expand (~50) `[APP]`
- [ ] Muted sessions bell (~60) `[APP]`
- [x] Sub-agent visibility toggle `[APP]`
- [x] Sidebar row drag-reorder (~110) `[APP]`
- [x] Search snippet highlighting in rows `[APP-WORKAROUND]` — same no-text-rects problem as #8
- [x] Tab drag-reorder (~90) `[APP]` — ALREADY BUILT: `model::reorder_tab`,
      driven from the drag in `tab_bar_system.h`. False gap
- [x] Tab context menu: Copy Navi URL, Close others (~50) `[APP]` — ALREADY BUILT:
      the menu, the real clipboard write and `model::close_others` were present. Fixed now:
      exact label, pinned-tab survival, two-pane reconciliation and scripted clipboard coverage.
      Only "close all" is genuinely missing.
- [x] Tab preview mode (~65) `[APP]` — and it is the feature that helps at 2000 rows
- [ ] ~~Space grouping~~ **BLOCKED** — sessions carry no Space; evidence above
- [ ] ~~Folder collapse-all~~ **BLOCKED** — depends on Space grouping
- [x] Tab scrollbar / overflow `[APP]` — ALREADY BUILT (Chrome-style overflow
      merged in `0ac0779`). Fixed now: native horizontal trackpad input reaches the
      shipped vector-wheel seam, manual scrolling is not immediately undone by active-tab
      visibility, and drag/vertical/horizontal behavior is covered under overflow.
- [x] Pinned tabs `[APP]` — pinning commits previews, Close others preserves pins, and
      pin state round-trips through a real two-process relaunch gate.
- [x] Closing tabs reconciles both panes `[APP]` — focused and unfocused pane ids are
      retargeted before persistence, superseded network/disk futures are retired without
      blocking or applying stale results, and stale split ids are ignored on restore.
- [x] Split restore focus `[APP]` — `focusedPane` is persisted explicitly instead of
      inferred from `active_tab`, which is ambiguous when both panes show one thread. Legacy
      settings without the field infer a unique active-pane match and otherwise fall back left.
- [x] Load-older ownership `[APP]` — each pane owns its future and session id; focus changes,
      tab switches and split close cannot redirect, block or apply another pane's completion.
- [x] Tab render allocation `[APP]` — HEAD measured 774 allocations/frame with 20 tabs;
      this branch measures 762, with a 920 runtime ceiling in `alloc-gate`.
- [x] Tab affordance names `[APP-WORKAROUND]` — tabs, pin markers, close buttons and the
      new-tab button carry cached app-owned `AccessibleName` metadata, exposed to scripted
      tests without rebuilding hidden label strings each frame. afterhours still exposes no
      OS accessibility-name or tooltip API; `vendor/` remains unchanged.
- [ ] ~~Window restoration~~ **BLOCKED** — needs multi-window architecture

Anything touching the sidebar must say how it behaves at 2000+ sessions.

## Search, settings, shortcuts — `search-settings-shortcuts.md`

- [~] Find operators `[APP]` — `state:` and `has:tool` shipped; `is:thinking`
      is blocked by there being no highlight path for a thinking row, not by
      the data model (the subtitle distinguishes them fine — docs/SEARCH.md S11),
      and shipping it now would break the tally rule
- [x] Session search across threads, Cmd+Shift+F — a local index over the disk
      cache, which says out loud how much of the corpus it could see. Needed a corpus;
      say whether it is a local index over the disk cache or a server verb
- [x] Command palette, Cmd+K (~250) `[APP]` — actions and threads in one list;
      every row raises the same request the button does
- [x] Snippet highlighting in sidebar search (small) `[APP-WORKAROUND]`
- [x] Send-key configuration, Return vs Cmd+Return (small) `[APP]`
- [x] Show/hide timestamps (small) `[APP]`
- [x] Typeface picker (small) `[APP]` — ALREADY BUILT: Settings → App font
      (Standard / Hyperlegible), wired and persisted. LIMITED as predicted: the library can load a font by
      path but cannot enumerate system fonts, so this can only offer the fonts
      we bundle. Shipping a fixed list is fine; "pick any system font" is not
      reachable and should not be promised.
- [ ] ~~Text weight picker~~ **BLOCKED ON YOU** — no bundled face has a bold or
      a variable axis, so the control would do nothing. Needs Roboto-Bold.ttf
      (Apache-2.0) and AtkinsonHyperlegible-Bold.ttf (OFL 1.1) committed with
      their licences: adding third-party binaries is your call, not an agent's
- [x] Theme picker with rotation (medium) `[APP]`
- [~] Custom theme editor (medium) `[APP]` — named swatches for the accent and
      the find band, each carrying a dark and a light colour. Not a freeform
      picker: the library has no colour input at all (gap #58), and a hex field
      would let someone pick #2b2b30 for text
- [x] Second global hotkey for the palette — Cmd+Shift+K. UNVERIFIED: nothing
      automated can press a global chord and I could not press it either
      (**small** — the focus-gated Carbon
      mechanism already exists in `native_extras.mm`; this registers one more
      chord against it)
- [ ] Keyboard shortcut recorder in Settings (medium) `[APP]` `[TEST-BLOCKED]`
- [ ] Composer keyboard shortcuts (small) `[APP]` `[TEST-BLOCKED]`
- [x] Navigation shortcuts, arrows in lists (small) `[APP]` — and the arrows
      have one owner now (`ecs/arrow_system.h`), the Esc pattern one key on
- [x] Find next/prev, Cmd+G (small) `[APP]`

**Cmd chords cannot be scripted.** The harness maps `CMD+` onto Control and
never holds Super, so every item above that is a Cmd chord needs its test to
drive state via a `# env:` line instead of the keystroke. Fixing it upstream is
`HandleKeyCommand` ignoring the parsed Super flag — the UI library is not ours
to change.

## Native, notifications, attachments — `native-notifications-attachments.md`

- [x] Expanded notification types — `feat/notification-kinds`: per-thread
      transitions (blocked / finished) replace the blocked-COUNT rule, which
      stayed silent whenever one thread unblocked as another blocked. Approval
      needed / input requested are not distinguishable in our model yet.
- [ ] ~~Expanded notification types~~ superseded by the line above: run finished, approval needed, input
      requested (~80) `[APP]` — native seam already exists in `native_extras.mm`
- [x] Quiet hours (~50) `[APP]` — `feat/quiet-hours`. Presets, not a picker:
      there is no clock control, and the window logic (which crosses midnight)
      is a tested pure function
- [ ] System menu bar: File / Edit / View `[APP]` — Obj-C++, not the UI library.
      NOT STARTED and deliberately so: a menu item's key equivalent is consumed
      by AppKit before the app sees it, so binding Cmd+F/Cmd+B there rewires
      chords that work today, and nothing here can verify a menu by hand
- [x] Image paste and drop in composer `[APP]` — the intake ships and the chip
      says plainly that this client cannot send an image yet (the wire wants an
      uploaded file id we have no path to). An Obj-C++ job behind the
      existing `extern "C"` seam, NOT a UI-library one. Clipboard images and
      file drops come from AppKit. Sending them is a separate question: the
      wire has fields, our client does not use them.
- [x] File picker `[APP]` — NSOpenPanel via the `.mm` seam, wired to the export
      destination (the upload tool it was specced against does not exist here)
- [x] Diff rendering for the edit tool `[APP]` — coloured per-line rows, and the
      classifier refuses to guess: a leading `-` is also every bullet list

Already built, do not rebuild: global hotkey (focus-gated), native
notifications, menu-bar extra, Spotlight seam, deep-link handler.

Most of this is unreachable by the scripted harness — notifications, menu-bar
items and file pickers are not in the widget tree. Where the honest answer is
"manual", the breakdown says so rather than inventing a gate.

---

# SESSION 2026-08-23 — worked the backlog on aspen

A fresh clone of hanabi now lives at `~/p/hanabi` on aspen (it was only on
juniper before), submodule initialised, `make test` green.

Everything below is on `main` AND on its own branch, both pushed. One theme per
branch, per the standing directive; each has a test that was checked to FAIL
against a build without it, because a test that cannot fail is not evidence.

| theme | branch | what it is |
|---|---|---|
| Context meter | `feat/context-meter-denominator` | real tokens over `tokens.context.budget`, stale marked, no constant anywhere |
| Session rename | `feat/session-rename` | right-click → Rename…, durable echo, no local optimism |
| Composer history | `feat/composer-history-walk` | Up/Down walk that thread's sent messages |
| Screenshot baselines | `feat/screenshot-baselines` | determinism target, three baselines, `make validate-screenshots` |
| Esc has one owner | `fix/esc-one-owner` | one keystroke dismisses one thing |
| Home shelf folding | `feat/home-shelf-collapse` | click a heading to fold a shelf; survives relaunch |
| Quiet hours | `feat/quiet-hours` | no banner inside the window; the midnight-crossing logic is a tested pure function |
| Notification kinds | `feat/notification-kinds` | per-thread transitions instead of a count; "a run finished" |
| Markdown headings | `feat/markdown-headings` | H1–H4, measure and draw proven in step over 452 renders |
| Date dividers | `feat/transcript-date-dividers` | a row per calendar day |
| Thinking indicator | `fix/thinking-demo-capture` | it existed; the demo knob was broken so nobody could see it |

## Five backlog items were already built — grep before you build

The parity doc warned a third of its claims were false. Verified against the
source today: **message delivery status rows** (`api::SyncState` +
`draw_sync_check`), **tab drag-reorder** (`model::reorder_tab`), **tab context
menu** Copy Navi URL and Close others (only "close all" is missing), **tab
overflow** (merged in `0ac0779`, this file's own log says so), and the
**typeface picker** (Settings → App font). The **streaming dots** entry was
worse than a false gap: the doc had been *corrected* from "built" to "not
built" by an agent who read the docs instead of the code.

## Found while working — small, real, still open

- Closing the find bar drops the composer's focus; the next Esc does nothing
  until you click back into the field.
- `scripts/review_shots.sh` still ends in a machine-wide `pkill -f hanabi.exe`,
  which kills other worktrees' captures. The three scripts on the gate path
  were scoped to their own binary; this one was not on that path.
- The thinking capture (`26_thinking_dark`) is deliberately NOT a baseline: its
  timer is stamped at a fixed 32s, but the reading can still flip to 33 if a
  run straddles a second boundary. Baseline it only with a frozen clock.
- afterhours gaps filed today: **#55** no `right_click_ui <name>` (context-menu
  tests are coordinate-keyed and retarget silently when a layout moves) and
  **#56** a freshly built `text_input` cannot be focused programmatically.

## Still in flight when this was written

Slash commands + model/effort pickers; find operators + timestamps + list
navigation; archive UI + pin + mute. Each on its own branch, unmerged.

---

# SESSION 2026-08-23, second half — the parity backlog, worked through

Everything below is merged into `main` and pushed, each theme also on its own
branch. `make test` on the merged tree: **17/17 unit, 72/72 scripted UI**,
launch gate PASS, `make validate-screenshots` 30/30.

| theme | branch |
|---|---|
| Command palette (Cmd+K) + a global chord for it | `feat/command-palette`, `feat/palette-hotkey` |
| Slash commands, model picker, effort picker | `feat/slash-commands`, `feat/model-picker`, `feat/effort-picker` |
| Undo toast, send-key setting | `feat/undo-toast`, `feat/send-key-setting` |
| Archive UI, mute, row drag-reorder, sub-agent toggle, tab preview | `feat/session-archive-ui`, `feat/session-mute`, `feat/sidebar-row-drag`, `feat/subagent-toggle`, `feat/tab-preview` |
| Thinking disclosure, tool fold defaults, code colour, find skips reasoning | `feat/thinking-disclosure`, `feat/tool-fold-defaults`, `feat/code-highlighting`, `feat/find-skips-thinking` |
| Transcript settings, link detection, minimap | `feat/transcript-settings`, `feat/link-detection`, `feat/transcript-minimap` |
| Cmd+G, cross-session search, sidebar snippets | `feat/find-next-binding`, `feat/cross-session-search`, `feat/search-snippets` |
| Theme rotation, custom colours | `feat/theme-picker`, `feat/theme-editor` |
| Image intake, file picker, diff rows | `feat/composer-images`, `feat/file-picker`, `feat/diff-rendering` |
| Screenshot gate, chunks 4-7 | `feat/shots-unbaselined`, `feat/shots-full-set`, `feat/shots-gate` |

## Two blocked on you, not on us

1. **Text weight picker** — needs `Roboto-Bold.ttf` (Apache-2.0) and
   `AtkinsonHyperlegible-Bold.ttf` (OFL 1.1) committed with their licences. No
   bundled face has a bold or a variable axis, so the control would do nothing.
   Adding third-party binaries and attesting redistribution is your call.
2. **Skills chip** — nothing in this client or on its wire knows what skills a
   session has. Evidence in `docs/breakdown/composer.md`.

## Things that were wrong with the harness, now fixed

- **`make test` could pass on a stale binary.** The unit targets depended only
  on their `.cpp`, so changing a header the test exercises re-ran the previous
  build. It produced at least one false green before it was caught. Every test
  now depends on every header.
- **A test that passed only while the machine was busy.** The slash script
  re-clicked the composer to select its draft; two clicks a few frames apart
  are a DOUBLE click, and that window is real time. It passed under load and
  failed at rest.
- **The thinking indicator was unphotographable**, so a screenshot of it showed
  a thread with no indicator in it.
- **Scoped kills.** `run_ui_tests.sh`, `screens.sh` and `measure_launch.sh` no
  longer `pkill` every hanabi on the machine. `scripts/review_shots.sh` still
  does — it is not on the gate path.

## Still open, and worth knowing

- **`is:thinking` / `is:tool` find operators** have no highlight path, so they
  could only ever answer "no matches" — which breaks the rule that nothing is
  counted that find could not paint. See src/ui/find_operators.h.
- **The system menu bar is deliberately not started.** A menu item's key
  equivalent is consumed by AppKit before the app sees it, so binding Cmd+F or
  Cmd+B there rewires chords that work today — and no agent here can verify a
  menu by hand.
- **A scrollbar draws over the modal sheets.** Visible as a grey rail through
  the settings panel and the palette; it is the pane behind bleeding through a
  layer boundary. Pre-existing, not from today's work.
- **A 2-6px measure/draw drift** in user turns, thinking blocks and spawn
  cards, identical on builds that predate all of today's transcript work. It
  looks like the library's `children()` box arithmetic. Chasing it moves every
  baseline, so it wants its own change.
- **Five agents filed an afterhours gap as #58 within the hour.** They are
  renumbered #58-#63 on main; if you have an unmerged branch citing one, check
  the number.
- **`git stash` is repo-global across worktrees.** Two agents popped each
  other's stash today (both restored). With parallel worktrees, use patch files.
