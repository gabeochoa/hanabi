# Hanabi — reconciled status (2026-08-02)
Audited every tracking doc (REQUIREMENTS / todo / DAYPLAN / POLISH_TODO / FEEDBACK)
against git history + the actual code. Many "open [ ]" items are STALE — already
shipped. This is the authoritative "what isn't fully done" list.

## ✅ COMPLETE & VERIFIED (docs were stale — these ARE done)
Phase AUTH (device-code + token-refresh) · Phase G (menu-bar, hotkey, notifications
+click-to-open, .app bundle, Spotlight index+deep-link) · Phase H icon sweep (0 raw
unicode chrome glyphs) · mock asset-split (file://-safe) · open-transcript-at-bottom
+ jump-to-bottom + stay-pinned · load-older (scroll-anchor + spinner + prefetch) ·
real tool-call fields · tabs (Chrome overflow + hscroll + right-click menu +
drag-to-reorder) · sidebar star right-align/no-bg · sidebar scroll text-render fix ·
layout-warn spam killed · composer input + message queue · settings (cache row +
Clear cache + cache-limit eviction + Account /whoami read) · local mock REST+SSE
server · archived Lucide sprite · thread-switch non-beachball + spinner · scrollbar
(draggable thumb + track-paging) · Archived→Views · live SSE multi-thread `● live` ·
fenced code blocks + Copy button · markdown bullets/rules · light-theme contrast ·
V1 status LED · V2 author grouping · V5/V6/V7 sidebar · status-header hues ·
F1 split-fragment id fix.

## 🔴 GENUINELY NOT DONE (verified in code, no shipping commit)
### Bugs / correctness
1. **Transcript SIGSEGV on large real transcripts** (~700 split msgs) — IN FLIGHT
   (ASan subagent). Highest priority; likely the real "no messages" symptom.
2. **HANABI_OPEN=<id> ignored on the http backend** — opens a restored tab instead.
   (Testing affordance; minor.)
### Feedback batch (live review) still open
3. **F2 — live latency vs web** — re-measure after the crash fix (same root as I1).
4. **V4 — tool-call icon outside its box** — repro only on real data (mock is fine).
5. **I1 — hover latency** — root-caused = frame-time (per-frame UI rebuild on heavy
   views). Real fix = reduce per-frame cost (T7-family), not a hover hack.
6. **I3-live — stay-at-bottom on SSE append** — render pin is correct; verify on live.
### Features not started
7. **I2 — split view** (snap tab left/right to see two threads) — big feature.
8. **Message hover actions** (copy/retry under a message, like web chat) — not built.
9. **Fold long tool piles / long code blocks** ("show N more") — helps F1 legibility.
### Perf
10. **T7 idle-frame cost** (~8.6ms/frame, zero headroom) — vendor-blocked TODAY
    (afterhours rebuilds tree every frame); afterhours maintainer fixes INCOMING →
    revisit when they land. Subsumes I1 + sidebar-fold jank.
### Platform / polish
11. **Follow-system light/dark** — needs an NSAppearance .mm shim (afterhours gap #16;
    settings "System" currently falls back to dark).
12. **Font-sizing bug** (vague, from an old screenshot) — needs a concrete repro.
13. **Scroll-direction toggle** — verified by math, not a live wheel feel-test.

## ⛔ BLOCKED ON EXTERNAL (afterhours maintainer — fixes INCOMING per Gabe)
- **V3 — tabs round only on top** (`top_round()`) — gap #25 patch (proven, in
  vendor_patches/) → apply upstream + bump submodule pointer.
- **Inline code pills / bold / italic** in message body — gap #22 patch (proven) →
  same. App-side wiring plan already documented in vendor_patches/22-*.patch.

## ⛔ BLOCKED ON EXTERNAL (navi API)
- **Backward/cursor pagination** for /messages — "load older" is a full refetch until added.
- **AI "waiting-on"/attentionState summary** per session — wire into Blocked view when it ships.
- **Settings/config read endpoint** — for deeper "verify setup" than /whoami.
- **AUTH 401 auto-refresh** — retry-on-401 handler (proactive token-refresh exists; this is the reactive path).
- **REST /messages envelope confirmation** + per-session workspaceId filter (par-msl/navi#4081 deploy re-verify).
