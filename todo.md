# Hanabi — TODO

A small, fast native desktop client for browsing/kicking-off conversation
sessions. Native (non-Electron): C++23 + afterhours (ECS/UI) + Sokol (Metal on
macOS), Make build. Runs standalone on a mock backend by default.

## Hard constraints (never violate)
- [x] Clean git history, pushed to a new GitHub repo (gabeochoa/hanabi).
- [x] No mention of the parent company anywhere in the repo.
- [x] Never hardcode/expose the real API (endpoint/keys/schema). A generic,
      runtime-configured HTTP adapter sits behind the same interface as the
      default in-memory mock. Mock is the default so it runs with zero config.

## Non-functional budgets (keep an eye on these)
- [x] Startup time: log + keep low (currently logs "Startup: N ms").
- [x] RAM: track RSS; keep lean. Sokol/Metal backend (fast) — chosen.

## Perf baseline (refreshed 2026-07-31 on cli:aspen — via scripts/run_tests.sh)
Captured with the headless one-shot render path (graphics init + full system
build + 45 frames + PNG capture + shutdown), zero-config mock backend. These
are the numbers the perf-regression gates watch (thresholds are the project's
hard budgets; edit the constants in scripts/measure_launch.sh / tests/e2e/
test_perf.cpp to tighten):
- **Startup (init → systems ready): ~19–28 ms**  (app's "Startup: N ms" log;
  same code path the windowed app uses).
- **FirstFrame (process start → first frame rendered): ~22–31 ms**  (new
  test-only "FirstFrame: N ms" log, gated on i==0 in run_headless_screenshot).
  GATE metric for launch. Budget < 250 ms (Phase P). ~10x headroom.
- **Peak RSS (headless, `/usr/bin/time -l`): ~47 MB** (~50.2 MB bytes).
  Budget < 250 MB (Phase X). ~5x headroom. Bounded by the transcript cache's
  20-msg x 5-thread cap (the only growth point); RSS plateaus while cycling
  through many threads (LRU evicts past 5).
- **Peak RSS (windowed w/ Metal window, `ps -o rss`): ~68–70 MB.**
- **Thread-switch latency (UNCACHED baseline): ~0.005 ms/switch** (tabflow
  focus + MockClient::get_session). Regression guard < 5.0 ms/switch.
- **Thread-switch latency (CACHED, Phase X): ~0.0005 ms/switch** (tabflow focus
  + TranscriptCache HIT, served synchronously, no fetch, no Loading flash).
  STRICT gate < 1.0 ms/switch — now REAL + PASSING in tests/e2e/test_perf.cpp.
Run gates: `make test` (unit+e2e+perf+launch) or `scripts/run_tests.sh`. Not
over-engineered — these are the baselines to watch as features + the cache land.

## Captured requests (running log)
- [x] MVP: session list + transcript, mock default.  (scaffold)
- [ ] Look as much like the desktop site as possible.
- [x] Single COLLAPSIBLE sidebar (merge icon-rail + chat list into one):
      folded = thin dense rail showing smart-view icons only
      (Home / Blocked / Review / Starred) + collapse/expand toggle; unfold to a
      full state (brand + search + New task + Settings + collapse; smart-view
      list w/ counts; folders; recent; a low-signal collapsed Archived section).
      Width animates via smoothstep. Toggle via header button AND Cmd+B.
- [x] Tabbed chat panels (VS Code editor-tab style) — open threads in tabs,
      tab between them, close (×), active-tab accent underline, Cmd+W closes.
      [Split view: DEFERRED — tabs first.]
- [x] Tab persistence across launches (open tabs + active tab persisted in the
      Settings save-file; restored once the session list loads).
- [x] Light + dark theming via a single swappable token set (src/ui/theme.h:
      Tokens struct + kDark/kLight; theme::set_mode() swaps at runtime;
      persisted in Settings; dark default). "Follow system" = TODO (see
      afterhours_gaps.md).
- [x] Smart views:
      Home  = digest (waiting-on-you → finished → self-running count)
      Blocked = tag==Blocked; Review = state==Ready (agent-verified);
      Starred = starred. Selectable, swap the main pane.
      + user Folders (Stars / Oncall / Experiments / Recent) + Archived.
- [x] High-signal sidebar: SHAPE-per-status glyph + bold-ish title ONLY when
      attention-worthy. Running threads dimmed/calm (no glyph). Parked &
      archived greyed. Status bar shows the blocked-on-you count.
      (model: api::ThreadState + api::ThreadTag on SessionSummary; mock supplies
      the sample states; http adapter leaves them Unknown/None = calm.)
      Phase 2.1 (DONE): dropped the sidebar text tag chip + plain attention
      dot; replaced with a dedicated SHAPE-per-status glyph so status reads by
      shape, not color alone — RED up-triangle (blocked/needs-you), GREEN
      diamond (review/agent-verified), BLUE dot (done). Drawn with afterhours'
      real primitives (draw_triangle / draw_poly(4) / draw_circle_v) via the
      per-widget with_on_draw_fg custom-draw hook (see SidebarSystem::draw_glyph
      + glyph_for in src/ecs/sidebar_system.h). Rows made denser (24px, was
      28px). Tag chips remain on the smart-view main-pane cards. VERIFIED:
      make -j4 clean, make test 1/1, headless --screenshot exit 0 valid
      1100x760 PNG.
- [ ] Kick off a task from a system launcher (Spotlight/global hotkey) AND from
      a macOS menu-bar (status bar) icon.  (Phase 4)
- [x] HTML design mock FIRST, to discuss interface/design before C++.

## Open questions
- [ ] afterhours: can tabbed panels + collapsible multi-pane be built on the
      current immediate-mode UI, or do we need upstream (panel/tab) support?
      -> research in flight (compare floatinghotel / wm_afterhours).

## Phased plan (dogfoodable ASAP)
- Phase 0 (DONE): scaffold — repo, mock+http adapters, ECS UI, git+push, constraints.
- Phase 1 (DONE): DESIGN LOCK — interactive HTML mock of the full desktop-like UI.
- Phase 2 (DONE): DOGFOODABLE CORE (C++) — reimplemented to match the mock:
      collapsible single sidebar (animated), VS Code tabbed panels, tab
      persistence, light/dark token set, high-signal rows. Mock default; http
      behind config. <- ship/dogfood here.  VERIFIED: make -j4 clean; headless
      --screenshot writes a valid 1100x760 PNG (exit 0); zero-config mock
      default confirmed. Perf baseline captured above.
- Phase 3 (DONE): SMART VIEWS wired — Home digest / Blocked / Review / Starred /
      folders populated from thread state (mock provides samples; http calm).
- Phase 4: macOS integrations — menu-bar (NSStatusItem) icon + Spotlight/global
      hotkey quick task kickoff.
- Phase 5: POLISH — perf pass (RAM/startup budgets), theming, native chrome.

## Done
- Repo scaffold, mock + http adapters, ECS UI (session list / transcript / status bar).
- Extracted Gabe's thread-management model -> docs/sidebar-model.md.
- Phase H — Native chrome icons (Lucide ISC single spritesheet). Replaced the
  ad-hoc unicode-text chrome glyphs (brand ✦, +, gear, collapse toggle, search,
  Home/Blocked/Review/Starred smart views, folder chevrons) with sprites blitted
  from ONE resources/icons/icons.png atlas (256x256, 4x4 grid of 64px cells,
  13 icons, white-on-transparent, tinted per-theme at draw time). Atlas map in
  resources/icons/icons.atlas + generated src/ui/icons_atlas.h; regenerated by
  scripts/gen_icons.py (fetches real Lucide SVGs, rasterizes via qlmanage +
  ImageMagick). resources/icons/LICENSE holds the Lucide ISC text. App-side
  icon() lookup + on_draw_fg blit in src/ui/icons.h; falls back to the legacy
  unicode glyph if the atlas fails to load. Status glyphs (triangle/diamond/dot)
  intentionally LEFT as drawn vector shapes. Headless --screenshot renders the
  icons; make test + launch/RSS gate still PASS. Swapping the sheet = replace
  icons.png + rerun gen_icons.py. (afterhours gap #13: sgl default pipeline has
  no alpha blending — worked around app-side with a blend pipeline.)

## Known issues
- (RESOLVED) Headless --screenshot hang: root cause was --screenshot arg
  parsing silently opening a window; fixed in src/main.cpp. Verified: exits 0,
  writes a valid 1100x760 PNG of the real UI.
- (RESOLVED, verified) Transcript body text now wraps correctly inside bubbles.

## Feasibility (answered — see docs/afterhours-feasibility.md)
- NO upstream afterhours change needed. afterhours ships tab_container();
  sibling app floatinghotel already implements VS Code-style closable tabs
  (tab_bar_system.h), animated collapsible sidebar (sidebar_system.h), and
  draggable split panes (split_panel.h) in app code. Phase 2 mirrors those.
