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

---

## VALIDATION AUDIT (2026-08-02) — every ask verified at code level

Method: for each item, confirmed the concrete code artifact exists in the named
file (not just the checkbox), then ran the full gate (build 0/0, TLS 0/0, make
test 8/8, perf PASS, make test-real 89 sessions + live /whoami). All green.

Feedback-blast items (files verified):
- #1 focus-gated hotkey — native_extras.mm: HotkeyFocusObserver + DidBecomeActive/WillResignActive register/unregister ✓
- #2 settings click-outside — settings_system.h: `!is_mouse_inside(panelRect)` gate before showSettings=false ✓
- #3/#4 fast-scroll blank — main_pane_system.h: velocity (s_lastScrollY) + extendUp/extendDown margin ✓
- #5/#6 multi-sub SSE pool — components.h struct LiveSub + liveSubs map; loader sync_subscriptions (per open tab) + background refetch→disk ✓
- #7 load-older anchor — anchorPending/anchorPrevMsgCount (components/loader) + prependedH scroll bump (main_pane) ✓
- #8 load-older indicator — loading_older_pill + "Loading older messages…" ✓
- #9 prefetch trigger — main_pane_system.h:requestLoadOlder=true when scrollY <= 2*viewH ✓
- #10 cache cap — settings.h/.cpp cache_cap_bytes + disk_cache trim_to_cap/touch_transcript + settings_system render_cache_limit_row + loader save_and_trim/touch ✓
- #11 single-tool right-align — metaW-computed cmdW ✓
- #12 clickable tool block — tool_block_expandable + tool_out_height + expandedPiles toggle + per-line tool_out_line ✓
- #13 star flash — sidebar: baked wash + star-id cache + skip_hover_override ✓
- #14 star right-align — row order: row_time(1822) before row_star(1861) ⇒ star rightmost ✓
- #15 settings polish — section_label + content-derived ph ✓
- tool-output \n — per-line tool_out_line rows (gap #24 workaround) ✓

Earlier-session items (regression-checked, still present):
- open-at-bottom scrollBottomPending (components/tab_model/main_pane) ✓
- selected≠hover sidebar (selected?selected_bg) ✓
- fold-all 28px + hover bg ✓
- fenced code is_code_fence ✓
- tab-void strip_bg=panel_bg ✓
- redundant-title→transcript_header ✓

Gaps documentation: afterhours_gaps.md now has a grouped INDEX + 27 numbered
gaps (#1–#31; animation sub-series relabeled AN-8..AN-12 to remove the dup #8)
+ WISHLIST A–H. New this session: #28 focus-gate, #29 single-hot-entity, #30
scroll-anchor-on-prepend, #31 stale-offset virtualization. (Found during audit:
subagents had logged #28/#29 as UNNUMBERED prose; #30/#31 were missing entirely
— all fixed.)

STATUS: all 15 feedback items + tool-output \n fix DONE and verified. 0 open.

---

## PADDING & ALIGNMENT SWEEP (2026-08-02) — full pass

Captured every surface at 1280×840 and inspected left-edge / right-edge / count-
column / icon alignment via cropped screenshots. Findings + fixes:

FIXED:
- Digest views (Home/Blocked/Review/Starred/Archived): title (20px) vs scroll
  body (24px) vs section labels (26px) = THREE left edges. Unified to
  kContentInset(24) — title, labels, cards now share one left edge. (575669a)
- Sidebar count column: VIEWS counts at panelW-8 vs FOLDERS/thread counts at
  panelW-12 (4px). Set smart-view row right pad to kCountRightPad(12); all count
  families now share one right edge. (575669a)

VERIFIED CLEAN (no change needed):
- Transcript: header/author/body share the 24px inset; tool-row border box at
  content edge with dur/check right-aligned; composer input+chip aligned; code
  block full-width with a 10px internal indent (mock-consistent block set-off).
- Sidebar: VIEWS icons+labels aligned; folder glyph+label+time+star columns
  consistent; star rightmost (per #14); fold-all chevron near edge.
- Settings modal: all section headers + segmented controls share left edge;
  even vertical rhythm; no dead space; × top-right.

NOTED (cosmetic, not fixed — low value / would be a design change):
- Code block renders as full-width tinted rows without a single rounded
  container/border (the mock has a `.block` card w/ a lang bar). Reads fine as a
  set-off block; wrapping it in one rounded container is a design refinement.
- Starred rows shift the time column ~18px left to reserve the star slot, so
  time x differs slightly between starred/unstarred rows (star-reservation
  tradeoff; avoids reflow-on-hover which is worse).

Gate: build 0/0, TLS 0/0, test 8/8, perf PASS, test-real 89 sessions. All green.
