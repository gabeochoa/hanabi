# Hanabi — design decisions (provisional)

Answers to the 10-question set. Marked (confirmed) once the user picks;
otherwise these are the working defaults driving Phase 2. Change freely.

1. Dogfood config: (default C) BOTH — env vars override an untracked local
   file `~/.config/hanabi/config.json`. Neither is committed. Real
   endpoint/token live only in the user's environment, never in the repo.
2. Thread status source: (default C) hybrid — backend may hint status; local
   heuristics + user star/park override. Adapter exposes an optional status
   field; mock supplies samples.
3. MVP write scope: (default B) kick off NEW tasks (spotlight + menu-bar);
   composer drawn but inline-reply send is Phase 3+.
4. "Ready to test": (default A) keep as a 4th top-level smart view.
5. Tab persistence: (default A) reopen exactly where left off (all tabs +
   active tab), like VS Code environment restore.
6. Theme: (default A) follow macOS system appearance, with a manual override
   that sticks. Single swappable token set either way.
7. Live updates: (default D) poll the session list; stream only the open
   thread. (All behind the adapter — mock is static.)
8. Global kickoff hotkey: (default C) global summon (Cmd+Shift+Space) + in-app
   palette (Cmd+K).
9. Menu-bar icon: (default A) dropdown with quick "new task" input + what's
   blocked-on-you; blocked-count badge on the icon.
10. Visual fidelity: (default B) clean chat+tabs shell + a custom hanabi mark
    (fireworks / 花火) for app + menu-bar icon. No real brand assets (constraint).

Deferred (explicitly out of MVP): split-view panes; chat/code toggle; mascot;
dotted-grid canvas (revisit after dogfood).
