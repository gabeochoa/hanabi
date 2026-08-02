# Hanabi polish TODO — Gabe feedback blast (2026-08-02)

Status legend: [ ] todo · [~] in progress · [x] done (commit) · [gap] logged to afterhours_gaps.md too

## CRITICAL / correctness
- [x] **1. Keyboard shortcuts only when window focused.** Global hotkey (Cmd+Shift+N, Carbon RegisterEventHotKey) is intercepting shortcuts even when hanabi is NOT the focused app — blocks Chrome shortcuts. Must gate on app-focused (or make the GLOBAL hotkey truly global-summon only, and in-app shortcuts window-focused). File: src/native_extras.mm / menubar. [afterhours-gap: OS focus state]
- [x] **2. Settings modal closes on ANY click inside it.** Click-outside-to-close is catching clicks INSIDE the panel. Only clicks on the backdrop (outside the panel rect) should close. File: src/ecs/settings_system.h.
- [x] **3. List rendering can't keep up at full-speed scroll** — components stop rendering (blank) then pop back when you stop. Virtualization/scroll perf under fast fling. File: main_pane_system.h (virtualization window / margin). [perf]
- [x] **4. Messages disappear as you scroll** (intermittent). Likely same virtualization culling bug as #3 (off-window window math drops visible items). File: main_pane_system.h.

## SSE / caching / data layer
- [x] **5. Live SSE disconnected while program open** — reconnected only when returning to thread. Should keep live-reading the last few opened threads (while their tab is open), write straight to the cache file, so switching to a tab shows fresh instantly. Don't wait until you're sitting on the thread to fetch. File: loader_system.h (multi-subscription + background write).
- [x] **6. Prefetch: keep last N opened threads live** (tab open) writing to disk cache directly (ties to #5).
- [x] **7. Load-older: don't jump to oldest.** When loading older messages, PRESERVE scroll position — insert older msgs ABOVE, keep viewport where it is (anchor on first-visible message). File: main_pane_system.h + loader.
- [x] **8. Load-older loading indicator** — right now it snaps/freezes with no feedback. Show a top spinner while fetching older. File: main_pane_system.h.
- [x] **9. Prefetch older on approach to top** with a debounce; if it looks like you're scrolling all the way back, fetch more aggressively. File: loader_system.h.
- [x] **10. Cache cap setting** — dropdown: 100MB / 1GB / 10GB / unlimited. When near cap, evict oldest: trim old messages from the least-recently-opened thread (tail the cache file, keep last ~10 messages of the longest-ago-opened thread). Prioritize evicting ARCHIVED threads first. Files: settings_system.h (UI) + api/disk_cache.h (eviction/trim) + settings.h.

## Visual / layout polish
- [x] **11. Single tool call: numbers not right-aligned.** A lone (non-pile) tool row's count/duration/status cluster isn't right-aligned like the pile version. File: main_pane_system.h tool_meta_cluster / render_tool_block.
- [x] **12. Tool call should be CLICKABLE to expand/see the calls** — even a single tool block should expand to show detail (input/result). File: main_pane_system.h render_tool_block (add click -> expand).
- [x] **13. Star hover flashes the whole thread row** — star + selected + hover states conflict (whole row flashes on star hover). Isolate the star hover so it doesn't retrigger the row bg. File: sidebar_system.h. (related to the selected!=hover fix already done, but the STAR hover still conflicts).
- [x] **14. Star should be right-aligned** in the thread row. File: sidebar_system.h.
- [x] **15. Settings modal needs design polish** — spacing/hierarchy/padding (see screenshot: labels cramped, big empty bottom, segmented control + rows need rhythm). File: settings_system.h.

## afterhours_gaps.md — things we'd expect "for free" from a real UI toolkit (LOG ALL)
- draggable scrollbar (asked for; building)
- click-outside-only modal dismissal (hit-test excludes children)
- OS window-focus state (for shortcut gating)
- scroll-anchor / preserve-position-on-prepend (load-older)
- virtualization that survives fast fling (don't blank)
- list "keep last-visible anchor" on content insert-above
- hover/selected state arbitration (don't stack washes)

## Already done this session (context)
- open-at-bottom on first open (ac2964c), selected!=hover (f46b17e), fold-all button + row titles (5437040),
  fenced code blocks (1c001cd), tab void + header + inset (f690db4).
