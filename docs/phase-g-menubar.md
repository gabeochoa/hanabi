# Phase G — Native menu-bar extra (NSStatusItem)

## Goal
A macOS menu-bar (status-bar) extra that shows hanabi's attention state at a glance
and offers quick actions, without opening the main window. This is the "ambient
presence" feature: a small icon in the system menu bar whose title/badge reflects
"N blocked on you", with a dropdown menu.

## Scope (this phase)
1. **NSStatusItem** created once, after NSApp + window exist (main thread).
2. **Title/label** reflects the blocked count: show a small firework/spark glyph
   plus the count when >0 (e.g. "✦ 2"), or just the glyph when calm. Text must be
   generic — NO parent-company names anywhere.
3. **Dropdown menu** with:
   - "hanabi" (disabled header row)
   - "N blocked on you" / "All caught up" (disabled status row, live)
   - separator
   - "Show hanabi" → activates the app + brings the window front (reuse the
     existing `metal_activate_app()` path).
   - "New task…" → activates app AND sets a flag the C++ side reads to open the
     composer (add `app.requestNewTask` bool if not present; wire minimally).
   - separator
   - "Quit hanabi" → normal terminate.
4. **Live updates**: the menu title + status row update as the blocked count
   changes. Poll-pull is fine: expose `extern "C" void menubar_set_blocked(int n)`
   and call it once per frame (or on change) from `app_frame`. Cheap; no new thread.

## Architecture (matches existing seam)
- New file `src/menubar.mm` (Obj-C++, auto-globbed by makefile `src/*.mm`).
- New header `src/menubar.h` with the extern "C" API:
    void menubar_install(void);          // create NSStatusItem (idempotent)
    void menubar_set_blocked(int n);     // update title + status row
    // action callbacks route back into C++ via function pointers or flags:
    // simplest: menubar sets atomic flags read by app_frame.
- The menu ACTIONS need to reach C++ state. Cleanest for immediate-mode: the .mm
  keeps file-static atomic<bool> flags (g_wantShow, g_wantNewTask, g_wantQuit) set
  by the NSMenuItem targets; expose `extern "C" bool menubar_take_show()` etc. that
  C++ polls + clears each frame in app_frame. Quit can call the C++ side or just
  `[NSApp terminate:nil]` directly.
- Install from `app_init` AFTER the window is created (NSApp must exist). If sokol
  hasn't created NSApp yet at init, defer: call menubar_install() on the FIRST
  app_frame instead (guard with a static bool). Verify which is true.

## Constraints (HARD)
- Do NOT edit anything under vendor/. If sokol/afterhours blocks something, log it
  to afterhours_gaps.md (next number is #22).
- No parent-company names in any file (icon, strings, comments). Generic only.
- Only touch: src/menubar.mm (new), src/menubar.h (new), src/main.cpp (install +
  per-frame flag poll), src/ecs/components.h (maybe: requestNewTask flag). Do NOT
  touch other agents' files.
- Build must stay 0-warning: `make -j4` AND `make -j4 HANABI_TLS=1`.
- `make test` must stay 3/3 + perf gate PASS.

## Verification
- Headless screenshot path is unaffected (menu-bar only installs in windowed run;
  guard so run_headless_screenshot never creates a status item — it would leak a UI
  element into the capture and slow launch). CONFIRM the perf gate still passes.
- Because the status item lives in the system menu bar (outside the app window), a
  window screenshot won't show it. Capture EVIDENCE another way:
    * `osascript -e 'tell application "System Events" to ...'` may be TCC-blocked.
    * Preferred: capture the full menu bar region via the CDP/AppKit path already
      used, OR log the NSStatusItem.button.title to stdout after install + after a
      blocked-count change, and paste that as proof. A screenshot of the dropdown
      is ideal if screencapture of a menu is permitted; if TCC blocks it, log-based
      proof + the code is acceptable (note the limitation honestly).
- Menu actions: prove Show/New task set their flags (unit-style: call
  menubar_take_show() returns true once after the item fires — or log it).

## Notes
- Keep it small and correct. This is an ambient affordance, not a second UI.
- The blocked-count derivation already exists in status_bar_system.h (count of
  s.tag == api::ThreadTag::Blocked) — reuse the same rule so menu + status bar agree.
