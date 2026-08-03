# ALL GABE FEEDBACK — hanabi (every prompt, 2 days) — DRIVE TO DONE
Legend: [x]=done+verified · [~]=partial · [ ]=open · [B]=blocked-external

## Founding constraints (2026-07-31)
- [x] native macOS afterhours client on aspen, called 'hanabi', git
- [x] HARD: no parent-company mentions in repo (guarded)
- [x] HARD: real API never exposed directly (mock default; real via local config only)

## Batch 1
- [x] V1 footer live->green network-activity LED fused w/ sessions (6d623fb)
- [x] V2 don't show thread name on every message (8eda930) — verified on real data
- [ ] I2 snap tab left/right = SPLIT VIEW (two threads) — big feature, TODO
- [B] V3 round only TOP of tab — blocked on afterhours gap#25 (maintainer incoming)
- [ ] I1 hover perf instant — root=frame-time; needs profiling/reduce per-frame work
- [x] F1 ton of tool calls no messages — FIXED: SIGSEGV OpenSSL race (d7a6374) + split-id (0f09517) + error-block (5ebb809); real transcript renders full text
- [x] V4 tool call icons inside the box — verified INSIDE on real data render
- [~] I3 stay-at-bottom on new updates — render pin exists; verify on live SSE append
- [x] V5 sidebar thread font match VIEWS (edaedc7)
- [ ] F2 live latency vs web — reduce (measure SSE apply)
- [x] V6 fill screen + show-more at bottom (edaedc7 + M2 fix 7723eae)
- [x] V7 indent show-more to thread indent (edaedc7 + M4 fix 7723eae)
- [~] S1 writing/compose API — RESEARCHED: POST /api/chat {sessionId?,message}->{sessionId,messageId,turnId}; steer POST /api/chat/steer {sessionId,message,steeringId?}. Wire = M6.
- [ ] V8 left-align "these" — TARGET UNCONFIRMED (need Gabe to point)
- [x] F3 settings buttons wired (verified) — deeper polish = M7
- [~] API asks -> navi PR thread (par-msl/navi#4081) — COMPILED in API_ASKS_FOR_NAVI.md; NOT posted (needs Gabe go for human-visible PR comment).
- [B] CTX afterhours fixes incoming (unblocks V3 + inline code pills #22)

## Batch 3 (M-series)
- [x] M1 message heights — error-block empty bubbles fixed (5ebb809) + SIGSEGV
- [x] M2 show-more hidden — fillCap reserves row (7723eae)
- [x] M3 VIEWS/FOLDERS gap — children()-sized (7723eae)
- [x] M4 FOLDERS indent match VIEWS — kRowLeftInset 22->16 (7723eae)
- [x] M5 star left of right-aligned timestamp (7723eae)
- [x] fold-all icon broken — clean triangles + clears scrollbar (5e1beae)
- [~] M6 text input + STEERING — text input DONE (99dbc4c: origin-absolute routing + local config chat_path -> composer renders + send-ready on real backend, verified). STEERING in flight (subagent: /api/chat/steer when agent Running).
- [ ] M7 settings page needs a ton of work — audit + expand

## Process asks
- [x] draft persistence (crash-safe local prompts/queue) — 8d05104
- [x] ponytail refactor review — REFACTOR_REVIEW.md
- [x] document every prompt — this file
- [ ] local-first: 5 ideas done (LOCAL_FIRST_IDEAS.md); BUILD outbox(#2)+offline-send(#5) next

## REMAINING OPEN (drive to done, in order)
1. M6 text input + steering on real backend (highest — blocks daily use)
2. M7 settings polish
3. I2 split view
4. I1 hover perf / F2 latency (frame-time; may need afterhours help)
5. local-first outbox + optimistic offline send
6. V8 (needs Gabe target), V3 + code pills (blocked on afterhours)

## SESSION 2 PROGRESS (polish loop) — updated statuses
- [x] V8 alignment: settings segmented controls full-width edge-hug (2e1c6aa)
- [x] Local-first foundation: SyncState enum + Message.sync (9d3c056)
- [x] Local-first #2/#5: crash-safe outbox + optimistic send + visible "· sent/sending/saved locally" (16c7dae)
- [x] Local-first #4: owned Markdown export + Settings Data section (f0b599a)
- [x] fold-all icon fixed (5e1beae); M2/M3/M4/M5 sidebar (7723eae); M1 (5ebb809)
- [x] M6 text input on real backend (99dbc4c) + steering (3b0df1b)
- [x] afterhours gap #28 logged (nested bubble child + on_draw_fg on bg div don't render)
- [~] M7 settings: Data section added; MORE possible (Connection/backend, About/version, Notifications toggle)
- [x] Local-first #1: 'refreshing…' read-state in transcript header (ddbaa26)
- [x] Local-first #3: sidebar search matches cached conversation CONTENT (389b4ac)
- [B] I1 hover / T7 idle-frame — RE-CONFIRMED vendor-blocked 2026-08-02 (reprofiled: 9.35ms/frame = 3.05 tick + 6.29 render; both halves in vendored code; sokol vsync-locked, no app-side fps cap; BeginUIContextManager clears tree so app-side idle-skip renders empty. Needs afterhours dirty-frame/retained-layout — gap #27). App-side perf already maxed (virtualization + measure-cache + app_singleton cache).
- [ ] I2 split view
- [ ] refactor quick-wins (REFACTOR_REVIEW.md): delete dead code, consolidate helpers
- [B] V3 tab top-round + inline code pills — afterhours patches ready, awaiting maintainer
- [ ] V8 remaining: sweep the rest of the UI for centered-should-be-edge (beyond settings)
- [?] API asks -> send to the thread that OWNS api PRs (NOT public PR comment) — need the thread id from Gabe

## MILESTONE: all 5 local-first ideas DONE (#1 read-state, #2 outbox, #3 local search, #4 export, #5 optimistic send) + WhatsApp-style local-vs-synced indicator.
## refactor quick-wins done: dead code (2329ca5), app_singleton cache (cdd116a).

## SESSION-2 LEDGER (polish loop, all pushed to origin/main, each build 0 + test 8/8 + screenshot-verified)
- 2e1c6aa V8: settings full-width segmented controls (edge-hug)
- 9d3c056 local-first SyncState foundation
- 916b5ed local-first sync-glyph render infra
- 16c7dae local-first #2/#5: optimistic send + crash-safe outbox + visible sync suffix
- c9b4348 gap #28 (nested bubble child + on_draw_fg on bg div don't render)
- 685ffd6 live Home screenshot refresh
- ddbaa26 local-first #1: 'refreshing…' read-state
- cdd116a perf: app_singleton() cache (REFACTOR_REVIEW #3)
- 2329ca5 refactor: dead estimate_height + tool_count removed (#2)
- f0b599a local-first #4: owned Markdown export + Settings Data section
- 389b4ac local-first #3: sidebar search matches cached content
- 3b4c3de M7: single-source version (src/version.h) + Settings About line
- aa3095a gap #29 (text_input no placeholder)
- ca5ff14 docs: I1/T7 re-confirmed vendor-blocked (reprofiled 9.35ms/frame)
- 2c51fc4 refactor 2b: drop unused colW param + dead truncated local
- 85f1a57 refactor 1c: shared fmtutil::to_upper/to_lower

## GENUINELY REMAINING (honest)
- I2 split-view (snap tab L/R) — large feature, unstarted, best with Gabe's UX steering
- REFACTOR_REVIEW 1b (relative-time 3-4 impls), 1a (measure↔render mirrors — HIGH risk, careful), 1e/1f/1g — real but lower-urgency
- [B] I1/T7 perf, V3 tab corners, #6/#10 inline code pills — all vendor-blocked (afterhours patches ready in vendor_patches/ OR gap #27/#28)
- API asks: API_ASKS_FOR_NAVI.md ready; need the thread that owns API PRs

## CHAT REDESIGN (Gabe: "chat screen needs a ton of work" + ChatGPT/Gemini/Navi-web direction + his build spec)
- [x] #1 centered 720px reading column (d6c8e68)
- [x] #2 killed green "hanabi" author label -> clean document text (899e7cf)
- [x] #3 tool calls reskinned as calm integrated cards (soft fill, no border, muted icon, status dot) (a4dd1c0)
- [x] #4a composer centered under the reading column (eacda3a)
- [x] #5 softened user bubble (899e7cf)
- [x] smooth eased scrolling (scroll perf) — vendor patch #30, SFINAE-guarded (3ea6cd3)
- [x] inline image rendering (agent surface: screenshots render in transcript) — render (b5b6354) + real-backend block wiring (c991635)
- [x] ponytail types pass: dead TimeBucket cluster (132 lines) + 3 dead AppComponent fields removed (804d894)
- [ ] #4b composer elevation/rounding + placeholder ("Message hanabi…") + circular primary send
- [x] #6 empty/sparse state: welcome greeting+chips (9993527) + sparse-thread vertical balance (195743c)
- [ ] inline HTML + big code/text block rendering (agent surface follow-on)
- [ ] remote image download-to-cache (only local/file:// rendered today)

## DESIGN CRITICS (2 adversarial: visual-craft + macOS-HIG) — consensus logged
- HIG-critic top items (traffic-light window controls, vibrancy/NSVisualEffectView, real NSSegmentedControl) need actual AppKit — hanabi uses a custom C++ toolkit by design, so these are ARCHITECTURAL, not polish. Logged, not actioned.
- craft-critic "contrast inversion" (titles dim, chrome loud): Home card titles already text_primary (fine); sidebar font + status glyphs were Gabe's explicit prior decisions (V5 lighten, shape+color state system) — NOT churned without his call.

## SETTINGS (Gabe: mirror navi web groups, mark API-blocked, include advanced, font choice)
- [x] Settings modal rebuilt to mirror navi web groups IN ORDER: Appearance (Theme + Font) / Behavior (Yap, Auto-archive, Memory) / Notifications (Sound) / Data (cache+limit+export) / Model / Advanced (branch URL, experiments, compaction, shortcuts, reset) / Account+Sign-out (912fb63)
- [x] All 3 themes functional + controllable; System now tracks OS appearance (bd4dda6, gap #16 fixed)
- [x] Font choice Default(Roboto)/Hyperlegible(Atkinson, OFL) — live swap + persist + startup restore; whole UI re-renders; no clipping in either font
- [x] Scrollable body (navi's full section set is ~1164px > window) — header fixed, sections scroll
- [x] API-settability marked per row: // TODO(settings-api): PUT /api/user/preferences.<field> for yapLevel/autoArchiveDays/memoryBackend/notificationSound/defaultModelId/branchOverrideUrl/enabledExperiments/compactionThreshold/keyboardShortcuts; // NOTE client-local for theme+font; reset needs a dedicated endpoint
- [ ] Functional wiring of the stubbed rows (needs a hanabi PUT-preferences client — not built yet)

## QA SWEEP (2026-08-03, post-redesign)
- [x] FIXED: settings section labels clipped at top (bare label divs top-anchor ~20px text) — AlignItems::Center + taller box (9313021)
- [x] Verified clean: Home sections (eyebrow font, no clip), transcript header (already centered), Blocked/Starred digest views (centered_wrap center=false — unaffected by transcript centering), Light+Dark themes, Hyperlegible font (whole-UI, no clip)
- Root cause noted for future: any bare label div with a ~20px font needs AlignItems::Center or it top-clips.

## GABE BUG REPORTS (2026-08-03 session) — all addressed
- [x] Input box invisible -> visible pill + placeholder (9f64b28)
- [x] Star not working (row-open swallowed the click) (9e7ef17)
- [x] Star position -> flush next to timestamp (9e7ef17)
- [x] Middle-click closes a tab (5ddd4db)
- [x] Folders collapsed by default + chevron show/hide (d776bae)
- [x] Settings freeze/crash: 2 real defects fixed (font-swap mid-render deferred; tool-card overflow) (8aafeac).
      Could NOT reproduce the crash on current HEAD via 3 attempts: headless settings render (exit 0),
      windowed + scroll the settings body (survived), windowed + click theme/font toggles (survived).
      Likely a STALE binary — rebuilt fresh output/Hanabi.app. If it persists post-relaunch, need exact repro action.
- [~] "Missing all icons/shapes" — all icons render in current build (toolbar/view/status/star/chevron verified);
      almost certainly a stale binary. Fresh .app rebuilt.
