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
- [ ] I1 hover perf / T7 idle-frame — needs afterhours help (incoming) or per-frame reduction
- [ ] I2 split view
- [ ] refactor quick-wins (REFACTOR_REVIEW.md): delete dead code, consolidate helpers
- [B] V3 tab top-round + inline code pills — afterhours patches ready, awaiting maintainer
- [ ] V8 remaining: sweep the rest of the UI for centered-should-be-edge (beyond settings)
- [?] API asks -> send to the thread that OWNS api PRs (NOT public PR comment) — need the thread id from Gabe

## MILESTONE: all 5 local-first ideas DONE (#1 read-state, #2 outbox, #3 local search, #4 export, #5 optimistic send) + WhatsApp-style local-vs-synced indicator.
## refactor quick-wins done: dead code (2329ca5), app_singleton cache (cdd116a).
