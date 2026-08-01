# hanabi — phased build plan + per-phase screenshot validation

Principle: build UP piece by piece. Each phase ends with a SCREENSHOT VALIDATION
GATE. A validation agent compares the phase's C++ screenshot against BOTH
(a) the split reference mock for that phase (docs/mock-phases/phaseN.html) and
(b) the pixel spec (docs/spec-metrics.md). We do NOT advance to phase N+1 until
phase N's UI matches — so bad layout can't accumulate.

Global validation rules (apply EVERY phase — the agent must check all):
- Window is 1180x760; no element overflows the window or its panel bounds.
- FONT SIZES match the spec type scale — flag any text that looks larger/smaller
  than the mock's equivalent element (title 12.5, smart label 13, row title 12.5,
  section label 10.5, counts 11, transcript h2 14). No invented sizes.
- PANEL LAYOUT matches: sidebar 280px (52px folded), title bar 38px, tab strip 38px,
  status bar 26px, transcript fills the rest. Panels aligned, no gaps/overlaps.
- PADDING & MARGIN match the mock per-element (row paddings, header paddings,
  section-label paddings, card paddings, gaps between items). Audit each container's
  inner padding and inter-item spacing against docs/spec-metrics.md; flag any that
  reads tighter/looser than the mock.
- VERTICAL CENTERING (check explicitly, every row/header/button): the glyph/icon,
  title text, count, and any trailing control must be VERTICALLY CENTERED within
  their row's height — no item sitting high or low. Check: sidebar thread rows
  (glyph vs title baseline), smart-view rows (icon vs label vs count), folder
  headers (chevron vs name vs count), the title bar (brand vs gear), tab strip
  (label vs close), status bar text, and the transcript header. Icons/glyphs in a
  fixed-size slot must be centered in that slot. This is the #1 "looks off" bug —
  call out ANY vertical misalignment with the specific element.
- Colors match tokens (dark default): bg 28/28/32, sidebar 22/22/26, accent 90/128/255.
- Alignment: glyphs, chevrons, counts vertically centered in rows; columns aligned.
- Compare SIDE BY SIDE with docs/mock-phases/phaseN.html rendered at 1180x760 —
  call out ANY pixel-level difference in position, size, weight, or color.
- Verdict: PASS only if it matches the mock; else list each mismatch + a fix.

---

## Phase A — App shell + panels (no data)
Build: window chrome (title bar w/ traffic lights + centered title), empty sidebar
(280px) with header (brand ✦ hanabi + New task + Settings + collapse), empty main
pane with a tab strip, status bar. Dark theme only.
Validate against docs/mock-phases/phaseA.html:
- [ ] Title bar 38px tall; 3 traffic-light dots 12px at far left, 8px apart; "hanabi" centered, 12.5px, name bold.
- [ ] Sidebar exactly 280px wide, bg rgb(22,22,26), 1px right border rgb(52,52,60).
- [ ] Header row ~40px; "✦ hanabi" left (mark 15px accent, name 13.5px bold); three 26px icon buttons right; collapse chevron rightmost.
- [ ] Status bar 26px tall at bottom, full width, 11px text.
- [ ] Main pane bg rgb(28,28,32); tab strip 38px with 1px bottom border.
- [ ] Nothing overflows; panels flush; no stray padding.

## Phase B — Collapsible sidebar (fold/unfold)
Build: collapse toggle + Cmd+B; folded 52px rail (icon-only smart views + collapse),
unfolded 280px full. Animated width.
Validate against docs/mock-phases/phaseB.html (capture BOTH states):
- [ ] Folded rail is 52px; only icons show (no text labels/counts); icons centered.
- [ ] Unfolded is 280px; brand text + search + labels visible again.
- [ ] Collapse toggle present in header both states; chevron flips direction folded vs unfolded.
- [ ] Smart-view icons identical position/size between states (16px svg in 18px box).
- [ ] Search field hidden in folded rail; visible unfolded (radius 8, 12.5px input).

## Phase C — Sidebar content: smart views + folders + rows (mock data)
Build: smart-view list (Home/Blocked/Review/Starred + counts), folders (Stars/Oncall/
Experiments/Recent) + Archived (collapsed), high-signal thread rows with SHAPE glyphs.
Validate against docs/mock-phases/phaseC.html:
- [ ] Smart items: label 13px/500, count 11px right-aligned tabular; active row selected-bg.
- [ ] Section labels "Views"/"Folders" 10.5px uppercase faint.
- [ ] Folder rows: chevron 12px + name 12px/600 + count 11px; body indented with 1px left guide.
- [ ] Thread rows dense (padding 3/8/3/10, ~1.25 line-height); title 12.5px.
- [ ] STATUS GLYPH shapes correct: blocked=RED up-triangle, review=GREEN diamond, done=BLUE dot; running/parked/archived = NO glyph.
- [ ] Attention rows bold+primary; running dimmed; parked/archived ~42% opacity.
- [ ] NO text tag chips in sidebar rows.
- [ ] Archived section collapsed by default, low-signal (plain count, no attention badge).

## Phase D — Sub-agents (nested, collapsed) + working ring
Build: threads with sub-agents show a disclosure chevron (collapsed default) + count;
hollow working ring right of the parent status shape when a sub runs; sub-rows on expand.
Validate against docs/mock-phases/phaseD.html:
- [ ] Sub-agent rows HIDDEN by default; chevron rotates on expand.
- [ ] Hollow working RING (outline, accent, ~9px) appears just right of the parent's filled status shape only when a sub is running.
- [ ] Expanded sub-rows indented 22px with twig connector; title 11.5px faint; child glyph 8px.
- [ ] Ring visually distinct from filled shapes (outline vs solid).

## Phase E — Tabs + transcript
Build: click row -> opens/focuses a tab; tab strip (active underline, × close, Cmd+W);
transcript with role-colored message bubbles, header, composer.
Validate against docs/mock-phases/phaseE.html:
- [ ] Tabs: 12.5px, padding 0 12px, max-width 220px, right 1px border; active tab bg=window bg + accent underline; × is 10px.
- [ ] Transcript header h2 14px/700 + sub 11.5px.
- [ ] Message role accent colors match tokens (user blue / assistant green / system amber / tool purple).
- [ ] Body text wraps inside the bubble (no right-edge clipping).
- [ ] Composer pinned at bottom (textarea + send button).

## Phase F — Smart-view screens (Home digest / Blocked / Review / Starred)
Build: selecting a smart view swaps the main pane to its screen; Home digest layout;
dotted-grid canvas background; light/dark toggle in Settings.
Validate against docs/mock-phases/phaseF.html:
- [ ] Home: "Waiting on you" numbered, then "Pinned to digest", then "Finished", then dimmed "Self-running (N)".
- [ ] Dotted-grid background on smart-view screens (22px grid).
- [ ] Blocked/Review/Starred cards match mock (card padding, chip on cards OK here).
- [ ] Light theme: toggle in Settings flips tokens; both themes match their mock variant.
- [ ] h1 20px; card names 13px; body 12–12.5px.

## Phase I — Interactive sidebar (match mock behavior)
The sidebar renders the right structure but is mostly static. Wire the mock's
real interactions (owner file: `src/ecs/sidebar_system.h`; shared state added
up-front to `components.h`).
Validate (screenshot + behavior):
- [ ] Folder headers collapse/expand on click (chevron rotates ▸/▾), state per
      folder held in a `collapsedFolders` set; body hides when collapsed.
- [ ] "Fold all folders" control in the Folders section header toggles every
      folder at once.
- [ ] Search field filters the thread tree live as you type (afterhours
      text_input); non-matching rows/folders hide; empty query restores all.
- [ ] Search no-results shows the empty state ("No matches for '<q>'").
- [ ] Star toggle on a row (or context affordance) flips `starred`, updates the
      Starred count + view immediately.
- [ ] Folded rail: smart-view icons show a small attention dot when their count
      > 0 (Blocked especially), matching the mock rail.

## Phase J — Transcript detail + sub-agent panel
The transcript renders role bubbles but lacks the mock's richer surfaces (owner
file: new `src/ecs/transcript_system.h`, extracted from main_pane; `components.h`
gets sub-agent view state).
Validate:
- [ ] Sub-agent panel at the TOP of the transcript when a thread has children:
      each sub-agent row with a working-ring (running) or status glyph (done),
      label, collapsible. NOT shown in the sidebar (per decisions).
- [ ] Tool blocks render distinctly (monospace-ish, "shell · 214 passed" style)
      vs assistant/user/system bubbles.
- [ ] Empty-thread state ("Nothing here yet") when a thread has 0 messages.
- [ ] Timestamps + role labels align; long text wraps; scroll works.

## Phase K — Settings panel + composer
Two mock surfaces the app is missing entirely (owner files: new
`src/ecs/settings_system.h` + `src/ecs/composer_system.h`; `components.h` gets a
`showSettings` flag + composer draft state).
Validate:
- [ ] Settings gear opens a real settings panel (overlay/sheet): theme
      light/dark/system toggle wired to `Settings::set_theme` + `theme::set_mode`
      live; closes on gear/Esc/outside-click.
- [ ] Theme switch takes effect immediately (icons re-tint, tokens swap) and
      persists across relaunch (now that the settings-path bug is fixed).
- [ ] Composer: read-only browse + a "New task" affordance (Cmd+N / the + in the
      title bar) opens a compose row to kick off a new thread (mock backend
      accepts it; reply-inline still deferred per decisions).

## Phase G — Native integrations (deferred; own validation later)
menu-bar NSStatusItem (blocked count + new chat), real Spotlight kickoff, global hotkey,
native notifications, offline cache. Validated per-item when built.

## Phase H — Native icons (single replaceable spritesheet)
Build: swap ad-hoc chrome icons to ONE Lucide (ISC) spritesheet — a single
`resources/icons/icons.png` atlas (monochrome/white on transparent, tinted per
theme at draw time) + a tiny `name->cell` map; the app loads the one texture at
startup and blits sub-rects. HTML mock uses one inline `<svg><symbol>` sprite
block. No installs, no per-icon files, no dependency, no Apple assets. Same
hanabi-neutral names on both sides so it stays swappable. See docs/icons.md.
Validate (screenshot + license audit):
- [ ] Chrome icons (gear, plus, search, sidebar toggle, chevron, folder,
      folder-grid, pin, archive, close, home, star) render from the single sheet,
      crisp, SF-adjacent look; tinted correctly for the dark theme.
- [ ] Status glyphs (triangle/diamond/dot) remain the drawn vector shapes (intentional).
- [ ] Repo contains NO Apple SF Symbol assets (grep audit); the set is Lucide ISC
      with resources/icons/LICENSE present; NO added package/CDN/dependency.
- [ ] Swapping the sheet is a single-file replace (icons.png + map) — documented.


## Phase P — Launch-time performance (HARD GATE: cold launch < 250 ms)
Goal: the app must LAUNCH in under 250 ms (cold), measured to first fully-rendered
frame. Current baseline: ~132 ms warm-path init (headless) / ~25 ms warm — already
under budget on the init metric, but this phase makes it a measured, enforced gate
on the REAL windowed launch (window create → first frame presented), and squeezes
it further. Do NOT touch vendor.
Work:
- Instrument true cold launch: from process start to first presented frame (not just
  the internal "Startup" init log). Add a one-shot timer that logs "FirstFrame: N ms".
- Measure cold (first run after build, caches cold) and warm; record both in todo.md.
- Optimize the hot spots without changing behavior: defer non-critical work off the
  launch path (lazy-load fonts/resources not needed for frame 1; avoid eager scans;
  ensure the mock seed + first list render is the only synchronous work); confirm the
  async loader doesn't block the first frame; check font atlas build cost; avoid
  debug-only overhead in release. Keep RAM in budget too (watch RSS doesn't balloon).
- Add a lightweight, repeatable measurement script (scripts/measure_launch.sh) that
  runs the app, captures FirstFrame ms + peak RSS, and prints pass/fail vs 250 ms.
Validate:
- [ ] Cold launch to first frame < 250 ms, measured on aspen, reported in todo.md
      with the exact number and the method.
- [ ] Warm launch number also recorded.
- [ ] Peak RSS still lean (see RAM budget below).
- [ ] scripts/measure_launch.sh exists and prints PASS (<250ms) / FAIL with the number.
- [ ] No vendor/afterhours edits (any needed capability -> afterhours_gaps.md).

## Phase X — Thread-switch speed + transcript cache + RAM budget
Two perf requirements, one phase (they trade off, so validate together):

### Fast thread switching via a transcript LRU cache
Problem TODAY: every thread open triggers a fresh async `get_session` fetch
(src/ecs/loader_system.h) — even re-opening a thread you just viewed. Switching
tabs should feel INSTANT for recently-seen threads.
Build:
- Add an in-memory LRU cache: keep the LAST 20 MESSAGES for the LAST 5 THREADS
  the user interacted with. Key by session id; evict least-recently-used past 5.
- On thread open/tab switch: if the id is in the cache, render from cache
  SYNCHRONOUSLY (no async round-trip, no Loading flash) — instant switch. Then
  optionally revalidate in the background (for the live backend) and swap in
  fresh data if it changed; the mock backend needs no revalidation.
- Cap cached messages at 20 per thread (most-recent) to bound memory; full
  history still fetched on demand when the user scrolls up (later; for MVP the
  cache is the fast path and a full fetch backs it).
- Keep it behind the api::Client abstraction — the cache lives in the app layer
  (AppComponent / a small TranscriptCache), backend-agnostic; mock + http both
  benefit. No vendor changes.
Validate:
- [ ] Switching between 5 recently-opened threads is instant — no Loading state
      flashes (verify: open 5 threads, tab between them; transcript appears
      immediately on switch).
- [ ] Cache holds ≤ 5 threads × ≤ 20 messages; the 6th distinct thread evicts the LRU.
- [ ] Opening a 6th (uncached) thread still works (async fetch path intact).
- [ ] No memory growth beyond the bound as you switch among many threads.

### RAM budget: keep total under 250 MB
Target: peak RSS < 250 MB (goal; current baseline ~50 MB headless / ~70 MB
windowed, so we have headroom — this phase keeps it there as features + the
cache land).
Build/measure:
- Record peak RSS (windowed, real use: open several tabs, switch, scroll) in todo.md.
- Ensure the transcript cache's 20×5 bound is the only unbounded-ish growth point
  and that it's actually bounded (measure RSS after cycling through 30+ threads —
  must plateau, not climb).
- Watch the font atlas / texture memory and image cache; cap or evict if needed.
Validate:
- [ ] Peak RSS < 250 MB in realistic use (multiple tabs open + heavy switching), in todo.md.
- [ ] RSS plateaus (does not climb unbounded) after cycling through many threads.
- [ ] No vendor edits; any memory knob afterhours doesn't expose -> afterhours_gaps.md.



## Phase R — Consolidation & Refactor (scheduled code-health pass)
Purpose: consolidate after the feature phases so the codebase stays clean as it grows.
This is a PURE REFACTOR — no behavior change, no visual change. It exists to pay down
the duplication that accreted while chasing mock parity (many explicit with_font_size
calls, repeated row/label builders, scattered pixel constants). Do NOT touch vendor/.

ENTRY GATES (all must be true before starting):
- [ ] All feature phases (A–F UI, G native, H icons) are green / accepted; no open
      feature work mid-flight that this refactor would collide with.
- [ ] Perf phases (P launch, X switch/RAM) are green — we have a recorded pre-refactor
      baseline (FirstFrame ms, cached-switch ms, peak RSS) to compare against.
- [ ] make test is green and screenshots match the mocks (this is the "known-good"
      baseline the refactor must not disturb).
- [ ] A pre-refactor screenshot set is captured and saved as the comparison baseline.

Scope:
- CONSOLIDATE the graphics-free model headers (src/ecs/thread_model.h,
  src/ecs/tab_model.h, src/ecs/transcript_cache.h): audit for overlapping types and
  logic, extract shared types, and settle a single `ecs::model` namespace convention
  for the graphics-free state layer.
  (DONE: thread_model.h, tab_model.h, and transcript_cache.h now all live under
  `ecs::model` — the tab flow was `ecs::tabflow` and the cache was bare `ecs`;
  both reconciled onto `ecs::model` so the "model" layer is one discoverable,
  consistent namespace.) These headers stay graphics-free (state/glyph/smart-view/tab/cache logic only, no draw calls).
- DEDUPE render helpers across the systems (sidebar_system.h, main_pane_system.h,
  tab_bar_system.h, and the transcript rendering path): pull the repeated row / label /
  section-header builders into shared helpers instead of copy-pasted blocks.
- TYPOGRAPHY helper: the layout-audit added many explicit with_font_size(<px>) calls
  (sidebar ~10, main pane ~8, tab bar ~2, status bar ~2). Replace the scattered raw
  font-size literals with a small typography helper keyed to the spec type scale
  (9…20 from docs/spec-metrics.md) so a size is named once, not repeated as a literal.
- TIGHTEN theme-token usage: ensure ALL colors come from theme::t (no stray rgb()
  literals left in the systems), and that any tint/color is read LIVE at draw time
  (not captured once at init) — this matters for the light/dark icon-tint work so a
  theme flip re-tints correctly.
- REDUCE per-system duplication: extract the shared layout constants (row height,
  paddings, gaps, glyph/icon slot sizes) into ONE place that mirrors
  docs/spec-metrics.md, so mock-parity numbers live in a single source of truth and the
  systems reference it instead of redeclaring magic numbers.
- Split any oversized system headers; consistent naming; remove dead code.

EXIT GATES (all must hold — this is what proves it stayed a pure refactor):
- [ ] make -j4 builds clean with 0 warnings.
- [ ] make test green (e2e + perf suite) throughout.
- [ ] Screenshots UNCHANGED vs the pre-refactor baseline (pixel diff ≈ 0) — a pure
      refactor changes no pixels.
- [ ] Perf gate still green: FirstFrame ms, cached-switch ms, and peak RSS all within
      noise of the pre-refactor baseline (no regression).
- [ ] Font sizes flow through the typography helper / spec scale; no stray size literals.
- [ ] All colors come from theme::t; no stray color literals; tint read live.
- [ ] Pixel constants come from the single metrics source; systems reference it.
- [ ] Model layer uses one consistent `ecs::model` namespace convention; headers stay
      graphics-free.
- [ ] vendor/ untouched; afterhours_gaps.md updated with anything the refactor wanted
      but couldn't do upstream.

## Phase API — Real-Backend Parity (Navi + AgentCloud)
Purpose: VERIFY (mostly a paper + fixture exercise, minimal code) that hanabi's HTTP
adapter — the one behind the mock+adapter seam, env-configured via
HANABI_BACKEND / HANABI_API_BASE_URL / HANABI_TOKEN (NEVER hardcoded) — maps cleanly
onto two real backends: the Navi API (proven in the wild by a Rust TUI client) and
AgentCloud (the successor orchestration service). The mock stays the zero-config
default; the real API is never hardcoded and never named with any internal URL.
Detail that doesn't fit here lives in docs/api-parity.md.

ENTRY GATES:
- [ ] The mock+adapter seam is in place: api::Client interface (src/api/client.h),
      mock_client.h as the zero-config default, http_client.h behind runtime config.
- [ ] Env-config path works: HANABI_BACKEND selects mock vs http;
      HANABI_API_BASE_URL / HANABI_TOKEN supply base URL + bearer at runtime (nothing
      baked into the repo).
- [ ] make test green with the mock backend (default) — the known-good baseline.

Scope — VERIFY the adapter maps onto the Navi API with no backend work:
- LIST: sessions list maps to the sessions endpoint, including `has_more` pagination.
- TRANSCRIPT: per-session messages map to roles (user/assistant/system) and block
  types (including tool-call / tool-result blocks).
- STREAM: the user-scoped SSE realtime channel maps to the adapter's optional
  streaming capability; the client filters events by session_id and handles the event
  kinds (text / thinking / tool-call / done / title-update / …).
- KICKOFF/CONTINUE: chat POST maps to kickoff (omit the session id for a new session)
  and continue; the response arrives over SSE.
- SEARCH: the hybrid (vector + keyword) search endpoint maps to the adapter's search.
- MUTATE: session PATCH maps to pin / archive (status) / title-edit.
- FOLDERS: the session-folders CRUD + reorder endpoints map to the sidebar folder model.
- AUTH: the device-code flow (request a code → user enters it at the auth URL → poll →
  receive a token; refresh via the refresh endpoint) fits the pluggable
  bearer/device-code auth — supplied at runtime, never baked in.
- EXTRAS (note as available, not required for MVP): export / share / fork / skills /
  schedules / nodes / preferences / workspaces.

Scope — note the AgentCloud (successor) adaptation:
- AgentCloud shares the same session / message / stream / event concept-shape, but is
  OpenAPI-spec-driven: its openapi.json is the capability source of truth (including
  default-model and harness enums the client should READ from the spec, not hardcode).
- It adds a structured `sessionOptions` object plus a "session-options patch" model for
  per-session config, and a durable typed-event journal (e.g. a recap/journal event
  wire-shape) for streaming.
- So targeting AgentCloud = generate/consume the client from openapi.json + adopt the
  sessionOptions-patch model + consume the journal events. Keep it behind the SAME
  adapter seam; no second bespoke client.

Scope — the TWO real gaps (present on BOTH backends) + hanabi's plan:
- GAP 1 — no explicit thread-STATE field. Neither backend exposes a high-signal state
  (blocked / needs-you / review / done); they only expose status (active/archived) +
  an is-processing flag + a sub-session status. hanabi must DERIVE its high-signal
  states client-side from those primitives (recommended near-term). If the server later
  adds a real `state` field, the adapter reads it when present and falls back to the
  derive rules when absent (OPTIONAL config-driven field).
- GAP 2 — no hard cancel/abort of a running turn. Steering / queueing works, but a true
  cancel would need a small new backend endpoint. Keep a `cancel`/abort adapter method
  in the interface (no-op on mock, gated by config) so hanabi is ready if a backend
  exposes it — but do NOT block on it.

VERIFICATION STEPS:
- Prove each Navi mapping above against a LOCAL, UNCOMMITTED, sanitized fixture per
  response shape (generic sample data — never real user data, never a real endpoint).
- Document the derive-client-side thread-state plan (the mapping from
  status + is-processing + sub-session status → blocked/needs-you/review/done) in
  docs/api-parity.md.
- Provide a config-only smoke test that hits a real backend, gated behind env vars
  (HANABI_BACKEND=http + HANABI_API_BASE_URL + HANABI_TOKEN), SKIPPED BY DEFAULT so CI
  and the default build never touch a real backend.

EXIT GATES:
- [ ] With NO config: the mock backend renders (unchanged zero-config default).
- [ ] The adapter maps ALL Navi endpoints above (list / transcript / SSE / kickoff /
      search / pin / archive / folders / auth) via config field-name overrides, proven
      with the local uncommitted fixtures — NO backend work required.
- [ ] Derived thread-state plan documented; adapter reads an optional `state` field when
      the backend supplies one, derives client-side otherwise.
- [ ] AgentCloud adaptation noted: consume openapi.json (models + default-model/harness
      enums), adopt the sessionOptions-patch model, consume the journal events — all
      behind the same seam.
- [ ] Streaming (SSE) and cancel/abort are OPTIONAL adapter capabilities (present in the
      interface, gated by config; cancel is a no-op on mock).
- [ ] The real-backend smoke test exists, is env-gated, and is SKIPPED BY DEFAULT.
- [ ] Repo audit: NO real endpoint / key / token / schema / internal URL / company name
      committed anywhere; still generic + mock-default. vendor/ untouched.
NOTE: everything stays behind the mock+adapter seam; the real API is never hardcoded and
the mock remains the zero-config default. Detailed parity findings live in
docs/api-parity.md (generic — no real values).
