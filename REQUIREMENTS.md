# hanabi — Requirements & Status

> Single source of truth for everything Gabe has asked for across the hanabi project,
> reconstructed from the git commit history (the most reliable record) + the in-repo
> planning docs (`todo.md`, `docs/phased-plan.md`, `docs/decisions.md`, `afterhours_gaps.md`,
> `README.md`) and the running scratch todo. Commit shas cited where identifiable.

## Overview

hanabi is a small, fast **native macOS desktop client** for the Navi AI assistant —
C++23 + the vendored **afterhours** ECS/immediate-mode UI library + **Sokol/Metal**,
built with Make. It presents a single collapsible sidebar (smart views + folders),
VS Code-style tabs, and a full transcript with an inline composer. It talks to the
Navi REST/SSE API through an adapter seam: an **in-memory mock backend is the default**
(zero config, deterministic), and a runtime-configured HTTP/SSE adapter drives it
against the real backend behind env/config.

**Hard constraints (never violate):**
- **No parent-company names anywhere** in the repo (code, docs, assets, history).
- **Never hardcode/expose the real API** (endpoint, keys, schema). Mock is the default;
  the real backend is reachable only via env vars / `~/.config/hanabi/config.json`,
  which live only in the user's environment.
- **Never edit the vendored `afterhours` submodule** — it's owned by external maintainers.
  When something is missing, log a numbered gap (or WISHLIST item) in `afterhours_gaps.md`
  and work around it in app code.
- **Dev in mock + `make test-real` before every push** — the pre-push "works with real data" gate.
- **One repo-mutator per file / per worktree** — parallel agents each own distinct files;
  parent merges sequentially; all gates green before push. (Also: don't re-pin the live
  screenshot every turn.)

---

## Done (shipped)

### Build / Tooling
- [x] Clean git history + pushed to new GitHub repo `gabeochoa/hanabi` (91d26bf, 5158102).
- [x] Make build; `run_tests.sh` harness + `make test/e2e/perf` targets + `tests/README` + perf baseline (60fdef9).
- [x] Opt-in TLS build (`make HANABI_TLS=1`); HTTPS support (f9f7fa9).
- [x] `make run` auto-enables TLS when OpenSSL present — one command to build+launch for https backends (a9cf608).
- [x] Auto-clean objects on TLS flip to avoid stale-object link failures (d9e27b9).
- [x] Separate object dirs (`objs-tls` / `objs-notls`) so alternating make/test/run modes keep incremental caches; mode switch just relinks (2cb1d58).
- [x] Parallel-by-default (`-j$ncpu`) + auto-ccache: bare make 22s→7s, warm rebuild 7s→0.1s (d69b156, merge 9a089e4).
- [x] `--screenshot` arg-parsing fix (was silently opening a window) (87b4754).
- [x] Config loader: `~/.config/hanabi/config.json` (env overrides file) + `config.example.json` + `.gitignore` guard (cb120af).
- [x] Reconcile env-var names (`HANABI_API_BASE_URL` + `HANABI_BASE_URL` fallback) (6437cc8).

### Backend / API adapter
- [x] Adapter seam: in-memory MockClient (default) + HTTP client behind the same interface; mock runs with zero config (Phase 0).
- [x] Derive light client-side thread state from a real backend + env-gated data-shape dump (34691fa).
- [x] Block-array transcript parsing — loads real backend sessions + transcripts (f9f7fa9).
- [x] Make a real (calm) backend's sessions actually show up (eac76a4).
- [x] Don't abort on an https URL without a TLS build (clean failure) (1ea7242).
- [x] Real folders from the API `workspace` field → `s.folder`; render dynamic folders; **drop hardcoded fake folders** (Stars/Oncall/Experiments); flatten day-grouping into a plain recent list (30414a2).
- [x] Drop the invented 'Recent' folder — only real API folders get headers (552fcf1).
- [x] On-disk stale-while-revalidate cache (session-list + transcript): cold first-paint 4109ms→warm 146ms (28x); http-only, atomic writes, never stores tokens (2403689).
- [x] Namespace on-disk cache by backend (base_url hash) so distinct servers never read each other's stale data (e49ebdf).
- [x] Migrate a pre-namespacing flat cache into the namespaced dir on first run (9b7a842).
- [x] Move streamed-reply collection onto a worker thread (was blocking the UI thread on real-backend send) (ff2ed1b).
- [x] Read-only real-backend smoke test `make test-real` — lists sessions + fetches a transcript against the ACTUAL backend, self-skips offline (ff2ed1b).

### Auth
- [x] Phase AUTH — in-app device-code login (config-driven, mock-testable, opt-in): request code → approve in browser → poll → persist token (0600, git-ignored) (90bc180).
- [x] Wire the REAL device-code flow to live navi-CLI endpoints (f872691).
  - (401-refresh is the one deferred AUTH item — see In progress.)

### Sidebar
- [x] Single collapsible sidebar (folded thin icon-rail ↔ full state), width animates via smoothstep; toggle via header button + Cmd+B (4a458e9).
- [x] Sidebar collapse toggle lives in the sidebar header (brand + chevron) (design f3122c9).
- [x] High-signal rows: shape-per-status glyph (red up-triangle=blocked, green diamond=review, blue dot=done) drawn with real afterhours primitives; bold title only when attention-worthy; running/parked/archived calm/greyed (9b1e3b5, 50e28cd).
- [x] Interactive sidebar: folder collapse/expand, fold-all, live search filter + no-results, per-row star toggle, folded-rail attention dots (af98291).
- [x] Wire `+` → composer and gear → settings overlay (4a8096d).
- [x] Redesign title bar; time-group the Recent catch-all; unify count column (b87d8cb).
- [x] Finer time buckets, per-row timestamps, unified status slot, search placeholder (790ace0).
- [x] Cap heavy time-buckets with "Show N more"; warning-triangle Blocked icon; running-ring glyph (65b8bd2).
- [x] Default-cap every bucket; strip `[P]` title prefix; mute cron rows (13d5092).
- [x] Move Archived from a folder into the **Views** section (Home/Blocked/Review/Starred/Archived) (160f481).
- [x] Swap star + timestamp order in chat rows (star left of time) (dfd33aa).
- [x] Size flex text children in pixels to stop NoWrap layout-overflow warning spam (1a97a55).
- [x] Match mock padding/margins + font sizes in the sidebar (5099295).

### Tabs
- [x] VS Code-style tabbed chat panels — open thread in a tab / focus existing; active-tab accent underline; per-tab close (×); Cmd+W closes active tab (774f988).
- [x] Tab persistence across launches (open tabs + active tab in the Settings save-file) (774f988).
- [x] Real tab-shape elevation ladder — 3-level ladder, top accent + content bridge, inter-tab gaps (ba7baa8).
- [x] Uniform tab widths + transcript text fills the pane width (217819f).
- [x] Vertically center tab labels in the 38px strip (1a97a55 / 1a97a55).
- [x] Strip `[P]` parked marker from tab labels (975959d).
- [x] Explicit tab-switching test (open 3, switch back-and-forth, invariants) (71e6296).

### Transcript / Chat
- [x] Sub-agent panel + distinct tool blocks + empty-thread state (4559dc0); SubAgent api type + mock data; nested sub-agents collapsed by default + hollow working-ring (a383e5d, 51ce51e).
- [x] Conversation bubbles → bottom composer → green done glyph (8c44ecf).
- [x] Doc-feed layout (navi-website style: author rows + prose, not bubbles) + capped ~720px centered reading column; short user prompt as compact right-aligned tinted bubble (859fd7e).
- [x] Strip inline-markdown delimiters so bodies read cleanly (0c216d6).
- [x] Collapse consecutive tool calls into an expandable pile ("N tool calls · …", default collapsed) (67ad62e).
- [x] Fold very long messages (>40 wrapped lines) with "Show N more lines"; **fix text-height over-estimation** (recalibrated font metrics) (177c3d5).
- [x] Hide the composer entirely on a read-only backend (no dead disabled input) (8aa6571).
- [x] Auto-stick-to-bottom while streaming (46cbbb6).
- [x] **Redact on-screen secrets** (JWTs, long opaque tokens) display-only + fix '(untitled)' header fallback (b5d30be).
- [x] Text clip + bottom padding fix; intra-message line culling (render only on-screen text) (9d351c3, e12a97a).
- [x] Clean user-bubble corners (roundness fix) (e12a97a).
- [x] **Perf+polish transcript: virtualize + memoize the doc-feed — 145ms→16ms/frame; match Navi web chat density** (c088ebd, merge 142b549).
- [x] Rich collapsible tool rows (chevron + wrench + mono cmd + count + run-duration + check) with nested per-node sub-rows; quiet "N sub-agents · verdict" rollup → chips; composer model selector + cost meter (part of chat overhaul c088ebd).
- [x] Per-thread reply drafts keyed by session id (was a shared static leaking between threads) (c31fc04).
- [x] Wire kickoff + reply/continue through the adapter seam (Phase SEND) (9a5019e).
- [x] Live token-by-token SSE assistant replies (Phase STREAM), mock-first & config-driven (6192665).
- [x] `Settings::set_theme` auto-persist + `Client::create_session` kickoff (mock impl; http degrades cleanly) (20c3ee8).

### Smart Views / Folders
- [x] Smart views: Home (digest waiting-on-you → finished → self-running), Blocked (needs-you), Review (agent-verified/ready), Starred; user Folders + Archived; selectable, swap main pane (774f988).
- [x] Home digest derives real attention state so it populates on the real backend (619fa89).
- [x] Home: collapse empty sections, elevate cards, tighten hierarchy (50e63fc); grouped-section previews show age not the state word (e45abb3).
- [x] Kill WAITING chip redundancy in grouped sections (state via header + discriminating detail only) + view-specific empty states + `HANABI_VIEW` capture affordance (d3962d1).
- [x] Blocked + Review render grouped-style (no per-card chip, age-only sub-line, dense rows); Starred stays mixed-state with chips (94986ad).
- [x] Cards: derive state-matched sub-line; never leak raw status under a chip; strip display-only `[P]` marker (c92ad4e, f374034, 975959d).
- [x] Preview-less cards collapse to a single tight row with inline right-aligned age (54aac6e).
- [x] Cold-cache skeleton loading state (6 calm placeholder cards) instead of a false "all caught up" (156773e); raise skeleton contrast (3ef24fe).
- [x] **Persist starred threads across relaunch** (was in-memory only) — Settings stores a starred id set, re-applied to freshly-fetched sessions (4059dd8).

### Perf
- [x] Startup/first-frame + peak-RSS + thread-switch perf regression gates (76d2254).
- [x] Transcript LRU cache (20 msgs × 5 threads) for instant switching + cached-switch perf assertions (4fdad28, af0eab9).
- [x] Best-of-N launch samples so the launch gate isn't flaky under machine load (best-of-3 → best-of-6) (43f06e6, 5c9decb).
- [x] Perf-gate isolated to mock so `make test` is deterministic regardless of local config (d9e27b9).
- [x] Budgets held: FirstFrame < 250ms (~22–31ms, ~10x headroom), peak RSS < 250MB (~47MB headless / ~70MB windowed), cached thread-switch < 1.0ms.

### Icons
- [x] Lucide (ISC) single-spritesheet icon system — one replaceable `icons.png` atlas + atlas map + LICENSE, no install, no SF-Symbols licensing risk (2a0899c, decision 847649b/563db65).
- [x] App-side `icon()` lookup + `on_draw_fg` blit (tinted, blended); replace unicode chrome glyphs in sidebar (8710d57, f9dd68c).
- [x] White-RGB atlas so theme tint applies (dark-mode icons were invisible) (da2d5ae).
- [x] `gen_icons.py` regenerator (fetches real Lucide SVGs, rasterizes); clock + repeat sprites added for cron rows (651f3de).
- [x] `TODO(icon-atlas)` convention for missing-sprite fallbacks (372c6e1).
- [x] **Phase H sweep**: all remaining unicode chrome glyphs routed through the atlas — added Lucide `close` (x) + `archive` sprites (atlas → 17 icons, 128×160); migrated the 4 raw `×` close buttons (tab / composer / settings / search-clear) + the Archived smart view; no known-missing sprites remain (0c99ec7). **Phase H COMPLETE** (250d9b4): the last raw unicode chrome glyph — the tool-pile count badge's `≡` (U+2261) — migrated to a Lucide `layers` sprite (atlas → 18 icons); 0 raw unicode chrome glyphs remain (only legitimate text arrows/bullets).

### Theme / Visual polish
- [x] Light + dark theming via a single swappable token set; dark default; runtime `set_mode()`; persisted (8320a4a).
- [x] Dark elevation system + fully retuned light theme + tab overflow cap (f77724f).
- [x] Named `theme::type` font-scale + `theme::scrim()` (refactor, pixel-identical) (7e524a5).
- [x] Composite low-alpha hover wash over each surface's backdrop (`theme::hover_over`) instead of raw token (718e0db).
- [x] Pre-blend tag-chip bg over card surface so soft tint reads as a pill (8a0db3f).
- [x] Deepen light-mode chips + card border for legibility (fe22503).
- [x] Two-cluster status bar; legible tokens; no backend leak (3125b36).
- [x] Respect macOS "natural scrolling" system setting; `HANABI_INVERT_SCROLL` override (8178d86, merge a1d3871, runtime-proven 9d351c3).
- [x] Tab-corner triangle glitch fix (afterhours gap #25 workaround) (9d351c3).

### macOS integration
- [x] Phase G — native menu-bar extra (`NSStatusItem`): blocked-on-you count `✦ N` + dropdown (Show hanabi / New task… / Quit) (c9b5c32).

### Tests
- [x] Headless e2e for state model + smart views + tabs + backend defaults (9183b61).
- [x] End-to-end screenshot harness + minimal test-only hover hook (9aff1f8).
- [x] Extract graphics-free thread/tab model + FirstFrame hook for headless tests (4e441a1).
- [x] Wait for the open thread's transcript to load before capture (not just the session list) (4f05218; afterhours gap #21).

### Docs / Design
- [x] HTML design mock FIRST (collapsible sidebar, tabs, smart views, light/dark, spotlight + menu-bar) (a161058); split into per-phase reviewable variants A–F (7bc2998); blue-sky v2 explorations (33a4597, f4b7404); improved messages-view mock v1/v2 (4e8a57b, 70e562a).
- [x] Confirmed + provisional design decisions from user Q&A (`docs/decisions.md`) (1d50e77, 19ff462).
- [x] Phased build plan with per-phase screenshot-validation checklists + pixel spec metrics (a2d4db2, 81049d1).
- [x] afterhours feasibility verdict (no upstream change needed; floatinghotel precedent) (87b4754, docs/afterhours-feasibility.md).
- [x] Icon strategy doc; animation-capability assessment; api-parity doc; web-client roadmap (63b4c24, 33a4597, 5330b3b, 764d32d).
- [x] `afterhours_gaps.md` — 26 numbered gaps + a maintainer-facing **WISHLIST** (A–H) of hand-rolled patterns that should be promoted upstream (0122fdc + ongoing).
- [x] README kept current (interactive composer, live SSE, device-code login, menu-bar, starred persistence, status glyphs, native icons; env table + src layout) (fd56744).
- [x] North-star framing: **hanabi is a PROOF that afterhours can carry a polished, team-usable app — polish is the deliverable, features wait** (7264a07).

---

## In progress / requested but not yet confirmed done

### Batch 3 — live-testing asks (2026-08-02), dispatched to worktree agents
- [ ] **B. Scrollbar** for the scrolling transcript/list section — afterhours has no scrollbar widget (log gap #26); add a temporary visual scrollbar (thumb sized/positioned from `HasScrollView` offset/content/viewport). Owned by scrollbar agent (`wt/scrollbar`) — RUNNING, uncommitted.
- [ ] **A. Open transcript at BOTTOM** (newest messages) + lazy-load older on scroll-up (memory-light). API: `/messages?limit=N` returns newest N + hasMore (offset/before ignored — no backward cursor yet). Interim: fetch newest N on open, render at bottom, "load full history" on scroll-up. Data layer on `wt/live-sse`; A-render blocked on the scrollbar agent's `main_pane` merge.
- [x] **D. Tool calls visible in the transcript on REAL data** — DONE: the http adapter splits `tool_call`/`tool_result` blocks out of assistant messages into `Role::Tool` messages carrying REAL fields (name→subtitle, command+node→text/`tool_node`, output→`tool_result`, `tool_status`, `tool_duration_ms` from completedAt−startedAt). The renderer prefers real fields (node/duration/status) and the pile count is the real message count — no hashed fakes left (msg_hash/tool_count removed) (0f31e2e, 734ba8b).
- [x] **E. Live SSE thread updates** — DONE: subscription binds on thread-open (torn down off the UI thread on switch/close — never leaks a worker), the worker flips an atomic + records the event time, the loader debounces → refetches the open thread's newest-N + reorders the sidebar in real time, and a **`● live` status-bar indicator** surfaces it (brightens on each event, settles to a calm connected tone; hidden on mock/unconfigured). SSE to `GET /api/v1/sessions/{id}/events`; `connected`/telemetry ignored. Extends the streaming-reply SSE to live updates (0122fdc data layer, b4ac5d8 indicator).
- [ ] **C. Archived → Views section** — DONE + pushed (160f481); unarchive-on-send already worked; **verify** on real backend.
- [ ] "Still a ton more work to make it look more real" — ongoing chat polish (overlaps D + the earlier 100-defect critique).

### Perf / jank (measured on Gabe's Mac)
- [ ] **T7 — kill the constant idle-frame cost** (~8.6ms EVERY frame, ~111fps, zero headroom). Root: afterhours rebuilds every widget + full-tree `solve_violations` + `render_cmds` every frame unconditionally; hanabi also rebuilds all rows + `distinct_folders()` sort per frame. Fix: dirty-flag skip-rebuild when nothing changed (the WISHLIST-B memo/dirty layer); cache `distinct_folders()`; avoid per-frame vector+sort/allocs in the sidebar. This is the remaining "as fast as possible / 120fps" ceiling. Do AFTER scrollbar + live-sse merge (owns main_pane + sidebar_system.h).
- [ ] **Sidebar-close jank / fold relayout** — the fold itself is fine (~10ms); the jank is the zero-headroom idle cost tipping any added work into visible stutter (esp. on a 60Hz external display). Subsumed by T7.

### Sidebar / layout follow-ups (routed to scrollbar agent)
- [ ] **Fold-all icon: right-align to full sidebar width.**
- [ ] **Collapsed-rail icons: left-align.**
- [ ] **Fix stale fold-all `kKeys[]`** (hardcoded stars/oncall/`__t_*__`/`__archived__`) — build from `distinct_folders(app)` instead (bug the agent introduced).
- [x] ~~Missing "archive" sprite in the atlas~~ — DONE: real Lucide `archive` sprite cut into the atlas; `close` sprite too; last `TODO(icon-atlas)` cleared (Phase H, 0c99ec7).

### Other open UI items
- [ ] **Layout-overflow warnings** — pixel-sizing flex children stopped the sidebar spam (1a97a55); watch for remaining NoWrap warnings elsewhere.
- [ ] **AI blocked-summary / "waiting-on" from the API** — server-side attention summary per session (Gabe's idea; API request #2 sent). Wire into the Blocked view when it lands. Nice-to-have on top of folders/pagination.
- [ ] **Tab rearranging** (reorder open tabs) — implied by the VS Code-tab model; not yet built (split-view is explicitly deferred).
- [ ] Scroll-direction toggle: verify both toggle states with a real windowed feel-test (was verified by math unit check, not a live wheel) — mostly resolved by 9d351c3.
- [ ] "Follow system" light/dark (auto-mirror macOS appearance) — token set + runtime swap exist; the OS-appearance probe is a TODO (afterhours gaps #1/#16; app-side .mm shim viable).

---

## Constraints & standing decisions

### Durable rules
- No parent-company names anywhere in the repo.
- Real API never hardcoded — mock is the default; real endpoint/token only via env or `~/.config/hanabi/config.json`.
- Never edit vendored `afterhours` — log a numbered gap / WISHLIST item and work around in app code.
- Dev in mock; `make test-real` (read-only real-backend smoke) before every push.
- One repo-mutator per file / per worktree; parent merges sequentially; all gates green before push.
- Don't re-pin the live screenshot every turn.
- Polish is the deliverable — hanabi is a proof afterhours can carry a polished team app; features wait behind polish.

### Key technical decisions
- **Backend config model:** env vars override an untracked `~/.config/hanabi/config.json`; mock is zero-config default; HTTPS is opt-in (`make HANABI_TLS=1`, or `make run` auto-detects OpenSSL). (decisions.md #1)
- **Thread status model:** start LOCAL — derive blocked/ready/waiting from session state (heuristics); add a backend status field later (adapter leaves room). `api::ThreadState` + `api::ThreadTag` on SessionSummary; http adapter leaves them calm/Unknown. (decisions.md #2)
- **"Review" view = READY FOR REVIEW**, not "you go test it" — the agent verifies; the user just looks. (decisions.md #4)
- **Tab persistence:** reopen exactly where left off (all tabs + active). (decisions.md #5)
- **Theme:** dark is the polished default; light lives in the same swappable token set; follow-system deferred. (decisions.md #6)
- **Live updates:** real-time SSE stream while a thread runs, behind the adapter; mock is static. (decisions.md #7)
- **Task kickoff / Spotlight:** user wants REAL macOS Spotlight (App Intents / Shortcuts / Service / URL scheme — not arbitrary code in Spotlight); deliver the best native path; in-app hotkeys later. (decisions.md #8 — research open)
- **Menu-bar icon:** clicking opens a NEW CHAT; badge = blocked-on-you count. (decisions.md #9)
- **Visual fidelity:** clean chat+tabs shell + a custom hanabi mark (fireworks / 花火) + dotted-grid canvas background; no real brand assets. (decisions.md #10)
- **Cache strategy:** stale-while-revalidate on-disk cache (session list + transcripts), namespaced by backend base_url hash, atomic writes, never stores tokens; http-only (mock stays pure). Plus a 20-msg × 5-thread in-memory LRU for instant thread-switching.
- **Virtualization:** doc-feed is virtualized + per-message memoized (only on-screen text built) → long transcript == short (~16ms/frame).
- **Perf budgets:** launch < 250ms; peak RSS < 250MB; cached thread-switch < 1.0ms.
- **Deferred out of MVP:** inline split-view panes; chat/code toggle; mascot; in-app command-palette hotkey; backend-supplied status field (now partially requested from the API).

### afterhours gaps / WISHLIST (maintainer feedback — logged, never patched in vendor)
- **26 numbered gaps** in `afterhours_gaps.md`, incl.: #6 headless capture can't supersample (hi-DPI); #11 no shimmer primitive; #13/#15 no alpha-blended rect/texture fills (`theme::over` workaround everywhere); #17 imm `text_input` ignores font-size/custom-bg; #18 no flex-grow (can't right-align a trailing element); #21 headless wait gates on list not transcript load; #22 styled label spans don't word-wrap (blocks inline markdown); #23 no list virtualization hook; #24 wrapped text ignores hard `\n` breaks; #25 `draw_rectangle_rounded` degenerate triangle for mixed corners; #26 no scrollbar widget.
- **WISHLIST (A–H):** (A) a real `measure_text`/wrap API — the #1 papercut; (B) a per-frame memo/dirty/virtualization layer; (C) real alpha-blended fills; (D) wrapping styled text for inline markdown; (E) a tween + disclosure + shimmer animation kit; (F) an OS platform shim (appearance / natural-scroll / open-url / activate / dpi / menu-bar); (G) blessed headless-render + input-injection test harness; (H) flex-grow, a mono font tier, a sprite-atlas helper, `scroll_to`/`scroll_to_bottom`, and a readable hover state.

### API features requested from the Navi maintainers
Sent to session `47bc4cf8` (which can open a backend PR). Request #1:
1. per-session `workspaceId` on `/api/v1/sessions`,
2. `?workspaceId=` filter honored,
3. cursor (backward) pagination on sessions + messages,
4. `attentionState` hint per session.

Request #2 (Gabe's idea): an AI **"waiting-on" summary** + `attentionState` per session (server-side beats client heuristics for the Blocked view).

### Live-verified API facts (2026-08-02, CLI Bearer token, `/api/v1`)
- `/sessions?limit=N` + `hasMore`; 237 total; **offset ignored**.
- `/sessions/{id}/messages?limit=N` = newest N ascending + `hasMore`; **offset/before ignored** (no backward cursor yet).
- `/sessions/{id}/events` = LIVE SSE (`text/event-stream`).
- Real messages: roles `user`/`assistant` only; tool calls = `tool_call`/`tool_result` **blocks inside assistant messages**.
- Folders: only `/api/v1/workspaces` (1: "Whole foods"); all sessions `workspaceId=null`; `/api/folders` = 401 (web UI uses cookie auth, our token is Bearer).

---

## Open questions / waiting on external

- [ ] **API PR par-msl/navi#4081 — MERGED by Gabe, waiting to deploy.** Delivers folder/pagination features. The adapter already parses `workspace`→`s.folder` + renders dynamic folders, so **real folders light up automatically on deploy — re-verify folders appear on the real backend after deploy.**
- [ ] **AI "waiting-on" / `attentionState` summary API** (request #2) — watch for it alongside the #4081 deploy; wire into the Blocked view when it lands.
- [ ] **Backward/cursor pagination** — until the backend adds it, "load older" is a full refetch of newest-N (A-item interim).
- [ ] **AUTH 401-refresh** — the one deferred Phase AUTH item (auto token refresh on 401).
- [ ] **macOS Spotlight kickoff mechanism** — determine App Intents vs Service vs URL scheme, and whether a signed/bundled `.app` is required (decisions.md follow-up #8).
