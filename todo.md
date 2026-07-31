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
- [ ] Startup time: log + keep low (currently logs "Startup: N ms").
- [ ] RAM: track RSS; keep lean. Sokol/Metal backend (fast) — chosen.

## Captured requests (running log)
- [x] MVP: session list + transcript, mock default.  (scaffold)
- [ ] Look as much like the desktop site as possible.
- [ ] Single COLLAPSIBLE sidebar (merge icon-rail + chat list into one):
      folded = thin dense rail showing smart-view icons only
      (Home / Blocked / Ready / Starred); unfold to pick a specific chat
      (full chat list w/ folders).
- [ ] Tabbed chat panels (VS Code editor-tab style) — open threads in tabs,
      tab between them.  [Split view: DEFERRED — phase it, tabs first.]
- [ ] Tab persistence across launches (reopen where you left off).
- [ ] Light + dark theming via a single swappable token set.
- [ ] Smart views (gchat-shortcuts style):
      Home  = digest/overview + anything asked into the digest
      Blocked = actively needs YOU to unblock (HIGH bar)
      Starred = pinned to top
      + user Folders.
- [ ] High-signal sidebar: attention/unread ONLY when a thread is DONE or
      WAITING ON YOU. Self-running threads stay quiet/dimmed. Rail badge counts
      Blocked only. Parked/muted state never nudges.  (see docs/sidebar-model.md)
- [ ] Kick off a task from a system launcher (Spotlight/global hotkey) AND from
      a macOS menu-bar (status bar) icon.
- [ ] HTML design mock FIRST, to discuss interface/design before C++.

## Open questions
- [ ] afterhours: can tabbed panels + collapsible multi-pane be built on the
      current immediate-mode UI, or do we need upstream (panel/tab) support?
      -> research in flight (compare floatinghotel / wm_afterhours).

## Phased plan (dogfoodable ASAP)
- Phase 0 (DONE): scaffold — repo, mock+http adapters, ECS UI, git+push, constraints.
- Phase 1 (NOW): DESIGN LOCK — interactive HTML mock of the full desktop-like UI
      (collapsible single sidebar, tabbed panels, light/dark, smart views,
      folders, menu-bar + spotlight affordance). Review + approve. No C++ yet.
- Phase 2: DOGFOODABLE CORE (C++) — reimplement to match the approved mock:
      collapsible single sidebar, tabbed panels, tab persistence, light/dark
      tokens, high-signal list. Mock default; http behind config. <- ship/dogfood here.
- Phase 3: SMART VIEWS wired — Home digest / Blocked / Ready / Starred / folders
      populated from session state (behind the adapter; mock provides samples).
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
