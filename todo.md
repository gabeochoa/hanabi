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

## Perf baseline (Phase 2, measured on cli:aspen 2026-07-31)
Captured with the headless one-shot render path (graphics init + full system
build + 45 frames + PNG capture + shutdown), zero-config mock backend:
- **Startup (init → systems ready): ~132 ms**  (from the app's "Startup: N ms"
  log; measured on the same code path the windowed app uses).
- **Peak RSS (headless, `/usr/bin/time -l`): ~49.8 MB** (49,823,744 bytes).
- **Peak RSS (windowed w/ Metal window, `ps -o rss`): ~68–70 MB.**
- Total headless run wall time: ~0.21 s real.
Not over-engineered — this is just the baseline to watch as features land.

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
- [x] High-signal sidebar: attention dot + bold title ONLY when
      DONE/WAITING-ON-YOU (state==Attention). Running threads dimmed/calm (no
      dot/bold). Parked & archived greyed. One tag chip max (Blocked/Review/
      Done). Status bar shows the blocked-on-you count.
      (model: api::ThreadState + api::ThreadTag on SessionSummary; mock supplies
      the sample states; http adapter leaves them Unknown/None = calm.)
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
