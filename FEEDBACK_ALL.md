# ALL GABE FEEDBACK — hanabi (last 2 days, verbatim), captured 2026-08-02

## Founding constraints (2026-07-31)
- Build a native macOS Navi client with afterhours (like floatinghotel), on aspen, call it 'hanabi'.
- HARD: don't mention anything Meta/company in the repo. HARD: don't expose the real API directly.

## Batch 1 (2026-08-02 ~15:43)
- [x] V1. footer "live" label -> green network-activity LED fused with sessions count (6d623fb)
- [x] V2. don't show thread name on every message (8eda930) — VERIFY heights (M1 says still off)
- [ ] I2. snap tab left/right to split view (two threads) — NOT STARTED
- [B] V3. round only the TOP of the tab, not bottom corners — BLOCKED on afterhours gap#25 (incoming)
- [ ] I1. hover highlight perf — should update instantly (root: frame-time)
- [~] F1. ton of tool calls but no messages — ROOT CAUSE = OpenSSL TLS-init race SIGSEGV (fix in flight) + split-id cache fix (0f09517)
- [ ] V4. tool call icons not inside the box
- [~] I3. new updates while scrolled to bottom -> stay at bottom (render pin exists; verify live)
- [x] V5. sidebar thread font too dark -> match VIEWS (edaedc7)
- [ ] F2. live view latency vs web — reduce
- [~] V6. fill screen with threads + show-more at bottom (edaedc7) — OVERSHOT: now too many, hides show-more (M2)

## Batch 2 (2026-08-02 ~15:44)
- [~] V7. indent "show N more" to match thread indent (edaedc7) — recheck after M4 indent fix
- [ ] S1. writing/compose API — look into it (research + API ask)
- [ ] V8. left-align "these" — TARGET STILL UNCONFIRMED (needs Gabe to point at element)
- [x] F3. settings buttons — already wired (verified) — but M7: settings needs a ton more work
- [ ] API. send API needs to the navi PR thread
- [B] CTX. afterhours maintainer fixes incoming (unblocks V3 + inline code pills #22)

## Batch 3 (2026-08-02 ~16:47) — NEWEST, top priority
- [ ] M1. message heights STILL messed up (visible: huge empty box above a message)
- [ ] M2. sidebar showing TOO MANY items -> hides show-more (V6 overshoot; cap to viewport w/ room for show-more)
- [ ] M3. gap between VIEWS and FOLDERS sections too large
- [ ] M4. FOLDERS indented too much — must match VIEWS indentation
- [ ] M5. thread timestamps right-aligned; star LEFT of timestamp, also right-aligned
- [ ] M6. still missing text input + steering (composer send to real backend)
- [ ] M7. settings page needs a ton of work

## Process
- ACTUALLY ADDRESS feedback (not just document). If not visible in the UI, FIX it.
- Document every prompt; subagent-validate with screenshots.

Legend: [x]=done+verified  [~]=partial/needs-verify  [ ]=open  [B]=blocked-external
