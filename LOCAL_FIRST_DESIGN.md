# Local-first build plan — 5 ideas + WhatsApp-style sync-state indicator

## Sync-state visual language (Gabe's ask: clear local vs persisted, like WhatsApp checks)
A single enum drives a small trailing glyph on anything that has a local vs server state:
  enum class SyncState { LocalOnly, Persisting, Synced, Failed };
Glyph vocabulary (drawn, theme-tinted, ~10px, in the metadata cluster):
  - LocalOnly   → single hollow/gray check  ✓ (gray)   "saved on this device only"
  - Persisting  → gray clock/half            ◔ (gray)   "sending to server…"
  - Synced      → double check              ✓✓ (accent) "confirmed on server"
  - Failed      → gray check + small !       ✓! (amber)  "not sent — will retry"
Applied to: (a) each user message you send (local→persisting→synced), (b) the
composer draft ("saved locally" pill), (c) queued/outbox items. A legend/tooltip
in Settings explains the marks.

## The 5 ideas -> concrete build
1. READ-PRIMARY CACHE (stale-while-revalidate): open renders instantly from disk
   cache, background revalidate, diff-in. Cache already exists — make it the default
   read path + never block on network to READ. Sync glyph on the HEADER: "cached ·
   refreshing" vs "live".
2. PROMPT OUTBOX (append-only local log): every prompt/steer written to disk BEFORE
   the network (extends drafts.json → outbox.json). Each entry carries SyncState.
   Drains on send/reconnect. THIS is where the WhatsApp checks live: your sent
   message shows LocalOnly→Persisting→Synced as it lands on the server.
3. LOCAL FULL-TEXT SEARCH over cached transcripts (SQLite FTS5 or on-disk inverted
   index). Instant offline search. Search results marked "local index".
4. OWNED DURABLE EXPORT: one action → ~/hanabi/threads/<id>.md (+ .json). Optional
   auto-mirror every viewed thread. Settings row "Export all / Auto-mirror".
5. OPTIMISTIC OFFLINE SEND: offline/failed send → accept into outbox, show the user
   bubble immediately with SyncState=LocalOnly/Failed + "will send when online";
   drain on reconnect. Reframes composer as local-first.

## Build order (after steering agent lands; coordinate file ownership)
A. types.h: add SyncState enum + Message.sync field (append-only). [shared — do first, alone]
B. disk_cache: outbox API (append/list/mark_synced/drain) + export API. 
C. components.h: outbox state + per-message sync tracking.
D. loader_system.h: write-to-outbox-before-send; mark synced on success; retry on reconnect.
E. main_pane_system.h: draw the sync glyph in the user-bubble metadata + header cached/live.
F. settings_system.h: Export row + sync-legend + Local-search toggle.
G. NEW util/local_search.* : the FTS index.
Each file = one mutator; serialize commits (learned: parallel git-index collisions).

## V8 ALIGNMENT AUDIT (from Gabe's screenshots — "hug the edge, don't center")
SETTINGS (fb2.png):
- Theme segmented (Light/Dark/System): buttons have big gaps + centered labels; the
  group should be tight/left-aligned (or full-width with equal segments flush).
- Cache row: "Clear cache" floats mid-right; should hug the RIGHT panel edge. Value
  "1.9 MB on disk" hugs left (good).
- Cache limit segmented (100MB/1GB/10GB/Unlimited): same spread-out issue as Theme.
- General: modal has dead horizontal space; controls should align to panel insets,
  not float centered.
POLISH/PERF BACKLOG (keep grinding):
- M7 settings: add Connection/backend row, About/version, Notifications toggle, sync legend.
- V8: edge-align all settings controls (segmented tight, buttons right-hug).
- I1 hover latency + T7 idle-frame (perf): reduce per-frame rebuild; afterhours help incoming.
- F2 live latency; transcript scroll perf on big threads.
- refactor review quick wins (REFACTOR_REVIEW.md): delete ~300 lines dead code, consolidate helpers.
