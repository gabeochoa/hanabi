# Hanabi — design decisions (CONFIRMED by user 2026-07-31)

1. Dogfood config: BOTH — env vars override an untracked local file
   `~/.config/hanabi/config.json`. Real endpoint/token live only in the
   user's environment, never in the repo.
2. Thread status source: START LOCAL — hanabi derives blocked/ready/waiting
   from session state (heuristics). Add a backend-supplied status field LATER
   (adapter leaves room for it; not required for MVP).
3. MVP write scope: READ-ONLY browse. Composer is drawn but disabled for MVP.
   (Task kickoff is a separate affordance — see #8/#9.)
4. "Ready" view = READY FOR REVIEW, not "you go test it". The AGENT tests /
   verifies; a thread lands in this view only once it's agent-verified and
   ready for the user to just LOOK. No manual test step expected of the user.
   (Rename the view accordingly: "Ready for review" / "Review".)
5. Tab persistence: reopen exactly where left off (all tabs + active tab).
6. Theme: follow macOS system appearance (single swappable token set).
7. Live updates: real-time stream while a thread runs (SSE-style), behind the
   adapter. Mock is static; live backend streams the open thread.
8. Task kickoff / Spotlight: user wants REAL macOS Spotlight to launch a task
   (not just an in-app palette). RESEARCH NEEDED — third-party Spotlight action
   integration on macOS is via App Intents / Shortcuts / a Services or Quick
   Action / URL scheme, NOT arbitrary code in Spotlight. Deliver the best
   native path. In-app hotkeys come LATER (e.g. Cmd+N = new chat).
9. Menu-bar (status-bar) icon: clicking it OPENS A NEW CHAT. Icon shows a small
   badge number = how many threads are blocked-on-you.
10. Visual fidelity: clean chat+tabs shell + a custom hanabi mark (fireworks /
    花火) for app + menu-bar icon + a DOTTED-GRID canvas background (esp. Home).
    No real brand assets (constraint).

Deferred (explicitly out of MVP): inline reply/send; split-view panes;
chat/code toggle; mascot; in-app command-palette hotkey; backend-supplied
status field.

## Follow-ups to resolve during build
- #8 Spotlight: determine the real macOS mechanism (App Intents vs Service vs
  URL scheme) and whether it needs a signed/bundled .app. Report options.

## Update (2026-07-31, session)
- THEME: dark-only for now. Light tokens stay in theme.h (single swappable
  Tokens struct) so enabling light later is a one-line set_mode() — but don't
  spend effort polishing light-mode layout yet. afterhours' token system makes
  the eventual switch trivial.
- SIDEBAR ROWS: no indent for folder threads. The shape-per-status glyph carries
  the meaning, so rows sit flush (left pad matches the folder header) to reclaim
  horizontal space for titles.
