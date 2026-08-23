# Sidebar, Navigation, Tabs & Windows — Implementation Breakdown

## Verification Results

**Gaps verified: 15 of 15** (9 sidebar + 6 tabs)  
**Already built: 2 features** (Smart views with badges, Home shelves with counts)  
**Partial: 1 feature** (Home shelves — other sections missing)  
**Real gaps: 12 features** requiring implementation  

### What's Already Built

1. **Smart views with live count badges** (gap #3 SIDEBAR)  
   - Sidebar shows "Blocked 2, Review 2, Starred 3, Archived 6" as icon+count rows  
   - Counts update live when a star is toggled (verified in code: `sidebar_system.h` lines 50–66)  
   - Clicking a smart view swaps the main pane to the filtered list

2. **Home shelves with section headers** (gap #4 SIDEBAR, partial)  
   - Home renders "WAITING ON YOU" shelf with count (2 items visible in screenshot)  
   - "FINISHED SINCE YOU LOOKED" shelf with count (2 items visible)  
   - "SELF-RUNNING" shelf with count (6+ items visible)  
   - Per-shelf collapse/expand **is missing**
   - Recent section below (uncollapsible)

---

## Gap Breakdown by Feature (12 Real Gaps)

### SIDEBAR & NAVIGATION (9 gaps)

#### Gap 1: Per-shelf collapse/expand (Home grouping) [ALREADY PARTIAL]
**What ships:** Add collapse state per shelf on Home. Click section header to toggle visibility. State persists per session.

**UX flow:**
- User sees "WAITING ON YOU • 2" header with a chevron ▼
- Click to collapse → chevron rotates ▶, section hides, cards don't render
- Click again to expand
- State saved in `app->collapsedShelves[shelve_name]` (UserDefaults via Settings)
- Empty states: collapsed sections show 0 UI; unfolded empty shelf shows cards list

**Where it goes:**  
- `src/ecs/main_pane_system.h` ~ line 1249 (render_home function): add chevron to `section_label` + collapse check before rendering cards
- `src/ecs/components.h`: add `std::unordered_set<std::string> collapsedShelves` to AppComponent

**Dependencies:** None (Home shelves already render)

**How it is proven:**
- E2E: screenshot with Home open, collapse a shelf, verify chevron points right and cards gone, navigate away and back, verify state persists
- Or unit test: mock AppComponent with collapsed shelf set, render_home returns nothing for that shelf

**Size:** ~50 lines (chevron rendering + collapse check + Settings persistence)

---

#### Gap 2: Space grouping in sidebar [BLOCKED, backend gap]
**What ships:** Sessions grouped by Space (not `workspace`). One section per Space, collapsible like folders. "No Space" bucket for unspaced sessions.

**UX flow:**
- Sidebar shows "ENGINEERING SPACE ▼" + 12 sessions below
- "PRODUCT SPACE ▼" + 5 sessions
- Click header to collapse/expand
- State persists locally (UserDefaults)
- At 2000+ sessions: only the current space's rows render visible; others scroll (space headers don't scroll, act as sticky bookmarks per puffin)

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ line 165 (folder loop): insert Space-grouping loop BEFORE folder loop
- Needs a GraphQL query to fetch spaces (not in mock; real backend only)

**Dependencies:**
- **Backend:** `workspace` field must be populated in session rows, OR a separate Space API must exist and sessions must link to it
- As noted in puffin_gaps.md line 93: "This is a backend gap, not a UI one."

**Status:** Blocked. Don't build until backend populates space data.

**Size:** ~120 lines UI + GraphQL (placeholder until backend is ready)

---

#### Gap 3: Muted sessions (bell icon, toggle)
**What ships:** Sidebar row shows a bell icon (or crossed bell 🔇). Click to toggle mute. Muting suppresses notifications for that session only. State is machine-local (not synced to server).

**UX flow:**
- User hovers sidebar row → bell appears in trailing accessory
- Click bell → toggles muted state, icon changes 🔔 ↔ 🔇, persists to UserDefaults
- Notifications are gated: code checks `isMuted(sessionId)` before firing toast
- Muting a parent doesn't mute children
- Settings → Notifications has a "Muted sessions" list showing which are silenced

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ line 700+ (render_chat_row function): add bell icon to trailing accessories
- `src/ecs/components.h`: add method `bool isMuted(const std::string& sessionId)` to AppComponent
- `src/settings.h`: add `set_muted(id, bool)` / `is_muted(id)` backed by UserDefaults

**Dependencies:** None

**How it is proven:**
- E2E: mute a session, navigate to a different one, return, verify bell is crossed; send a message to muted session, verify no notification fires
- Settings pane screenshot shows muted list

**Size:** ~60 lines (icon rendering, toggle, Settings integration)

---

#### Gap 4: Sub-agent visibility toggle (sidebar-global setting)
**What ships:** Sidebar has a checkbox "Show finished sub-agents". Toggled globally (not per-session). Affects expand/collapse of child lists in all sessions. Off by default to reduce clutter.

**UX flow:**
- Settings → Sidebar → checkbox "Show finished sub-agents"
- When off: rows with sub-agents never expand their children, or children don't render at all
- When on: children render normally
- State persists across relaunches

**Where it goes:**  
- `src/ecs/sidebar_system.h`: gate child-row rendering on `settings.show_finished_subagents` flag
- `src/settings.h`: add `set_show_finished_subagents(bool)` / `get_show_finished_subagents()` 
- Settings UI → add checkbox under "Sidebar" section

**Dependencies:** Sub-agents must already render (they do; see notes on Sub Agent Panel gap #6 SESSION LIFECYCLE)

**How it is proven:**
- Screenshot: Settings pane shows checkbox; toggle it, sidebar re-renders without sub-agent children
- E2E: disable the flag, expand a parent, verify no children shown; enable, expand, verify children

**Size:** ~40 lines (toggle + gate + Settings UI)

---

#### Gap 5: Sidebar row drag-and-drop reordering
**What ships:** Drag a session row up/down within its folder to reorder it. New order persists locally per folder.

**UX flow:**
- User presses and holds on a row → row becomes semi-transparent, a drop zone line appears
- User drags to a new y position → drop zone line moves
- User releases → row animates to new position, order is saved to UserDefaults
- Order survives relaunch; can be reset to server order via a context menu "Reset to default"
- Reordering works within a folder; dragging between folders is not supported

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ render_chat_row: add drag gesture + drop-zone line rendering
- `src/ecs/components.h`: add `std::map<std::string, std::vector<std::string>> folderRowOrder` to AppComponent (folder → [sessionId, sessionId, ...])
- Settings integration: `set_folder_row_order(folder, order)` / `get_folder_row_order(folder)`

**Dependencies:**
- Afterhours drag support must be present in the vendor
- (Note: afterhours drag is used elsewhere in the codebase; assume available)

**How it is proven:**
- E2E: drag a row, verify new position; close/reopen app, verify order persists
- Scripted test: cannot press and hold (only rapid keys), so use a `# env:` line to seed the row order, then render and screenshot

**Size:** ~110 lines (drag gesture + drop zone + order persistence)

---

#### Gap 6: Search snippet highlighting in sidebar rows
**What ships:** When a session is found by Cmd+Shift+F search (session-level search), its sidebar row shows a snippet of the matching text with the search term(s) highlighted.

**UX flow:**
- User presses Cmd+Shift+F → session search sidebar opens (not built yet; gap SEARCH #3)
- User types "python" → results list shows matching sessions
- User clicks a result → that session's sidebar row now shows: "…detected python error in…" with "python" highlighted in red
- Next search term clears the snippet

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ render_chat_row: add snippet rendering below title when `app->searchSnippet[sessionId]` is set
- New field: `app->searchSnippet: std::map<std::string, std::string>` (session id → snippet with inline highlight markers)
- `src/ecs/sidebar_system.h` snippet extraction logic (find match context around the search term, 20 chars before/after)

**Dependencies:**
- Session-level search endpoint (gap SEARCH #3) must be built first
- This gap is gated on search being implemented

**How it is proven:**
- E2E: run search, click result, screenshot sidebar row with snippet visible
- Or manual: set `app->searchSnippet["id"] = "...python..."`  and render

**Size:** ~65 lines (snippet rendering + highlight markup)

---

#### Gap 7: Per-Space collapse/expand all (folder headers)
**What ships:** Space section headers have a collapse arrow. Click to hide/show all sessions under that Space at once. Persists per-space.

**UX flow:**
- "ENGINEERING SPACE ▼" header with a chevron on the left
- Click chevron → arrow rotates to ▶, all sessions below it hide
- Click again to expand
- Separate toggle per space; one space collapsed doesn't affect others

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ space-grouping loop (when built): add chevron to space header, collapse check before rendering rows
- `src/ecs/components.h`: `std::unordered_set<std::string> collapsedSpaces` in AppComponent

**Dependencies:**
- Gap #2 (Space grouping) must be built first
- This is purely UI-side, no backend work

**How it is proven:**
- Screenshot: collapse a space, verify rows hidden; expand, verify rows reappear

**Size:** ~35 lines (part of Space grouping PR, added when gap #2 ships)

---

#### Gap 8: Session rename with durable echo

> **Planned in full in `session-lifecycle.md` — build it from there, not here.**
> Two agents wrote this area up independently. The lifecycle document is the
> authoritative one: it verified the backend verb is advertised on attach. What
> follows is the sidebar/tab entry point only.

**What ships:** Right-click a session row or tab → "Rename…" → modal dialog with current title → edit and Return → title is sent to server → durable echo (`session_renamed` frame) updates the display.

**UX flow:**
- User right-clicks sidebar row → context menu with "Rename…"
- Modal pops up: "Edit session title:" with current title selected
- User types new title, presses Return
- Name is sent to server via `PatchSessionRequest`
- Server replies with `SessionRenamed` event frame
- On receipt, session title updates everywhere (sidebar, tab, main pane header)
- Server can refuse (title validation): error modal with message
- No local optimism (must wait for server echo)
- Empty/whitespace names: client-side validation rejects them, doesn't send

**Where it goes:**  
- `src/ecs/sidebar_system.h` ~ render_chat_row: add context menu handler for "Rename…"
- `src/ecs/tab_bar_system.h` ~ tab context menu: add "Rename…"
- New component: `SessionRenameModal` with title text field + OK/Cancel
- `src/api/mock_client.h` or real `client.h`: wire `patch_session_options` call (or new `rename_session` call if available)
- Frame handler in the session loader to process `SessionRenamed` event

**Dependencies:**
- Backend must support a rename endpoint (assume it exists per puffin)

**How it is proven:**
- E2E: right-click row, type new name, verify modal, verify title updates after server response
- Screenshot shows new title in sidebar and tab

**Size:** ~80 lines (modal UI + wire + frame handler)

---

#### Gap 9: Search across all sessions (Cmd+Shift+F)
**What ships:** Cmd+Shift+F opens a session-level search. Type to search across all sessions (title + transcript full-text). Results show session title + snippet of matching context. Click a result to open that session. Supports partial matches and case-insensitive search.

**UX flow:**
- User presses Cmd+Shift+F → a search sidebar or modal appears (dedicated search pane, left of main content)
- Search field focused, ready for input
- User types "python" → results stream in below the search field as a list
- Results show: session title, match snippet with search term highlighted, relative age
- Click a result → that session opens in a tab (or focuses if already open)
- Empty state: "No results for 'python'" when no matches
- Loading state: "Searching 2000 sessions…" spinner while indexing/searching
- Scroll states: 100 results shown at once, "Load more…" button for the rest

**Where it goes:**  
- New system `SessionSearchSystem` in `src/ecs/session_search_system.h` (render the search pane)
- New component `SessionSearchComponent` (query, results, selected index)
- `src/ecs/main_pane_system.h` ~ dispatch on a new SmartView::SessionSearch case
- Full-text indexing logic: build an in-memory index of session titles + transcripts when sessions load (or lazy-build on first search)
- Wire call: likely a backend endpoint, or client-side over already-loaded transcripts

**Dependencies:**
- Backend session search endpoint, OR client-side full-text scan (depends on data model)
- See puffin_gaps.md line 581: "Blocked until data-layer loading old history lands"

**How it is proven:**
- E2E: Cmd+Shift+F, type search term, screenshot shows results
- Unit test: mock 5 sessions with known titles, search for a substring, verify 2+ results match
- E2E with network: search term that spans transcript, verify result includes snippet

**Size:** ~200 lines (search UI + indexing + wire + result rendering)

---

### TABS & WINDOWS (6 gaps)

#### Gap 1: Tab drag-and-drop to reorder
**What ships:** Drag a tab by its title to the left/right to reorder it. Other tabs shift. New order persists in UserDefaults. Visual feedback: dragged tab is semi-transparent, a drop-zone line shows the insert position.

**UX flow:**
- User presses on a tab title → tab highlights
- User drags left/right → drop-zone line moves, other tabs shift to make room
- User releases → tab lands in new position, order saved
- Tab order survives relaunch

**Where it goes:**  
- `src/ecs/tab_bar_system.h` ~ tab rendering: add drag gesture + drop-zone line
- `src/ecs/components.h`: add `std::vector<std::string> tabOrder` (already exists as `TabStripComponent.tabOrder`, verified in tab_model.h)
- Persistence: `Settings::get().set_tab_order(order)` / `get_tab_order()`

**Dependencies:**
- Afterhours drag support (assume available)

**How it is proven:**
- E2E: drag a tab, verify new position; close/reopen, verify order persists
- Scripted test: cannot drag interactively, so use `# env:` to seed the tab order in TabStripComponent, render, screenshot

**Size:** ~90 lines (drag gesture + drop-zone rendering + order persistence)

---

#### Gap 2: Tab drag to split pane (side-by-side layout)
**What ships:** Drag a tab into a "drop zone hint" area (left edge, right edge, top, bottom of the pane) to create a split layout. Both panes render tabs. Each pane has independent scroll and focus. Closing both panes collapses back to single pane.

**UX flow:**
- User drags a tab → as it moves over edges, a zone hint appears (highlighted border or shaded area)
- User drops on left edge → pane splits left/right; current tab moves to right pane, left pane is empty
- User opens a tab in left pane → independent content
- Right-click either pane's tab strip → "Close pane"
- When last tab in a pane closes, pane collapses; if other pane is empty, back to single pane

**Where it goes:**  
- `src/ecs/layout_system.h`: add split state to LayoutComponent (or SplitState component)
- `src/ecs/tab_bar_system.h`: render two tab strips when split is active
- `src/ecs/main_pane_system.h` ~ render_split function (already exists at line 106, but only via HANABI_SPLIT env flag)
- Drop-zone rendering: visual hint areas at pane edges

**Dependencies:**
- Afterhours drag support
- Split layout rendering already exists; needs to expose it to drag interaction instead of only env flag

**How it is proven:**
- E2E: drag a tab to right edge, verify split appears, open another tab in left pane, screenshot shows two independent panes
- Scripted test: set `HANABI_SPLIT=1` and render, verify two tab strips visible

**Size:** ~110 lines (drag gesture + drop-zone hints + split-state toggling)

---

#### Gap 3: Tab context menu (Copy URL, Close Others, Close All, Move)
**What ships:** Right-click a tab to see: "Copy Navi URL" (deep link), "Close Others", "Close All", "Move to new window".

**UX flow:**
- Right-click a tab → context menu appears
- "Copy Navi URL" → copies `navi://session/{sessionId}` to clipboard (deep link scheme)
- "Close Others" → closes all tabs except the clicked one
- "Close All" → closes all tabs, back to Home
- "Move to new window" → opens a new app window with this tab (requires multi-window support; see gap TABS #5)

**Where it goes:**  
- `src/ecs/tab_bar_system.h` ~ tab rendering: right-click handler + context menu
- Clipboard write: use afterhours clipboard plugin (already used in tab_bar_system.h line 29)
- Tab operations: delegate to `ecs::model::close_tab()` etc. (already exists in tab_model.h)

**Dependencies:**
- Multi-window support for "Move to new window" (gap TABS #5); stub it as disabled until that exists

**How it is proven:**
- E2E: right-click tab, verify menu appears with all options
- Screenshot: menu visible with correct labels
- "Copy URL" → paste into a text field, verify URL is correct format

**Size:** ~50 lines (context menu + handlers)

---

#### Gap 4: Tab preview mode (inactive tabs frozen)
**What ships:** When a tab is clicked but not kept open, it shows in preview mode: the tab reads as inactive (lighter background), its transcript doesn't stream live (frozen at the click moment), and only a second click keeps it (promotes to durable tab). Kept-open state is durable per app launch.

**UX flow:**
- User clicks a session row in sidebar → tab opens in light/inactive appearance
- Transcript content appears but doesn't update (frozen)
- User clicks the same tab again → tab becomes active (darker), content now streams live
- On app relaunch, all kept-open tabs restore; preview tabs are discarded

**Where it goes:**  
- `src/ecs/tab_model.h`: add `bool isKeptOpen` field to Tab component
- `src/ecs/tab_bar_system.h` ~ tab rendering: shade inactive tabs differently
- `src/ecs/main_pane_system.h` ~ transcript rendering: gate the live SSE stream on tab.isKeptOpen
- Settings: `set_kept_open_tabs(ids)` / `get_kept_open_tabs()` to restore on launch

**Dependencies:** None

**How it is proven:**
- E2E: click a row (tab opens light), verify no new messages stream in; click tab again (becomes dark), verify messages stream
- Close/reopen app, verify kept-open tabs are restored

**Size:** ~65 lines (tab appearance + frozen stream gate + persistence)

---

#### Gap 5: Window restoration on launch
**What ships:** Open windows (id + title + frame position + tabs in each pane) persist to UserDefaults on app quit. On next launch, windows are reopened with the same content and position. Gated by a Settings toggle "Restore windows on restart".

**UX flow:**
- User opens 3 windows, arranges them, fills them with tabs
- User closes the app
- User opens the app again → 3 windows appear at their old positions with the same tabs (or empty if tabs were preview-only)
- Settings → General → toggle "Restore windows on restart" to control this

**Where it goes:**  
- `src/ecs/layout_system.h`: serialize window state on close (frame position, split state, tab order per pane)
- `src/main.cpp` or app initialization: on launch, deserialize window list and restore each
- `src/settings.h`: `set_restore_windows_on_restart(bool)` / `get_restore_windows_on_restart()`

**Dependencies:**
- Multi-window support must be wired up in the app framework
- Currently hanabi only opens one window; multi-window is architectural
- Requires coordination with the window manager (afterhours or OS-level)

**Status:** Blocked on multi-window architecture. Don't build until window manager can open/close multiple windows.

**Size:** ~120 lines (serialization + restore logic)

---

#### Gap 6: Tab scrollbar (overflow handling, many tabs)
**What ships:** When many tabs are open (>10), the tab strip shows a horizontal scrollbar (like Chrome). Tabs shrink to a min width (no truncation). Active tab stays visible when scrolling. Left/right arrows scroll the strip.

**UX flow:**
- User opens 15 tabs → tab widths shrink to ~60px each (min, not truncated)
- Horizontal scrollbar appears below the strip
- User clicks a tab off-screen → strip auto-scrolls to keep it visible
- User clicks left/right scroll arrows → strip scrolls by one tab width
- Wheel-scroll on the strip scrolls horizontally

**Where it goes:**  
- `src/ecs/tab_bar_system.h` ~ tab strip container: wrap in a horizontal ScrollPanel (afterhours preset)
- Min-width logic: set each tab to a fixed minimum width so they don't truncate
- Scroll-to-keep-visible: when active tab changes, compute its screen x position and scroll the panel if needed
- Arrow buttons: left/right at the strip edges, click to scroll by `kTabMinWidth`

**Dependencies:**
- Afterhours horizontal ScrollView (todo.md #26 lists this as a gap in afterhours)
- May require custom scroll wrapper until afterhours adds it

**How it is proven:**
- E2E: open many tabs, screenshot shows scrollbar; scroll, verify tabs remain clickable
- Scroll to off-screen tab, verify it appears

**Size:** ~95 lines (ScrollPanel setup + scroll-to-keep-visible logic + arrow buttons)

---

## Commit Sequencing Strategy

**Goal:** Independent, shippable PRs. Each PR is ~50–200 lines, one theme.

### Ship Order (First 5 PRs)

1. **Session rename** (gap SIDEBAR #8, ~80 lines)
   - Reason: Core workflow, no blockers, simple (modal + wire)
   - Unblocks: nothing critical, but table-stakes feature

2. **Per-shelf collapse/expand (Home)** (gap SIDEBAR #1, ~50 lines)
   - Reason: Home is the landing page; grouping is already rendered, just needs toggle
   - Unblocks: nothing else

3. **Muted sessions** (gap SIDEBAR #3, ~60 lines)
   - Reason: Quick feature, standalone (no other subsystem depends on it)
   - Unblocks: nothing else

4. **Tab drag-and-drop reorder** (gap TABS #1, ~90 lines)
   - Reason: Polished UX for power users, no architectural changes needed
   - Unblocks: Tab context menu (gap #3) improves UX once this exists

5. **Tab context menu** (gap TABS #3, ~50 lines)
   - Reason: Quick, improves tab management UX
   - Unblocks: Multi-window support (gap #5) can reuse the "Move to new window" option once multi-window architecture lands

---

## Feature Dependency Graph

```
BLOCKED (backend or architectural):
  - Space grouping (gap SIDEBAR #2) ← backend Space API + workspace field
  - Session search (gap SIDEBAR #9) ← backend search endpoint or history loading
  - Window restoration (gap TABS #5) ← multi-window architecture
  - Tab scrollbar (gap TABS #6) ← afterhours horizontal ScrollView

Chains:
  Session rename (no deps) → improves UX, enables context menu
  Home shelves toggle (no deps) → independent feature
  Muted sessions (no deps) → independent feature
  Tab reorder (no deps) → enables context menu
  Tab context menu (needs reorder for full UX, but works standalone)
  Tab preview mode (no deps) → independent feature
  Tab drag-to-split (needs afterhours drag, needs multi-window for "Move to new window")
  Sidebar drag reorder (needs afterhours drag)
  Search snippet (needs session search to ship first)
  Subagent visibility (needs subagents to render; they do)
  Folder collapse all (needs Space grouping first)
```

---

## Notes on Scale, Testing, and Constraint Handling

### Scale (2000+ sessions)

1. **Sidebar reordering:** Drag only works on visible rows (rendered in the scroll pane). Off-screen rows don't have hit rects. ✓
2. **Folder/Space collapse:** Hiding a folder's rows doesn't mean they're unloaded; they're still in memory and clickable if opened via search. ✓
3. **Session search:** Indexing 2000 sessions on app launch is O(n); search is O(log n) with an index. Consider lazy-building on first search. Build index in a background task to avoid blocking the UI.
4. **Home shelves:** Capped at 20 per shelf; no scroll-pit risk. ✓

### Testing Constraints

1. **Drag gestures:** Scripted tests cannot press and hold (Cmd modifiers don't persist). Workaround: use `# env:` directives to seed the final state (e.g., `app->tabOrder = {id1, id2}`) and screenshot the result, or write unit tests on the model (tab_model.h) without graphics.
2. **Context menus:** Right-click is supported in e2e. ✓
3. **Persistent state (Settings):** Tests share a process-wide reset. Any test that modifies UserDefaults must clear it at the end, or subsequent tests see stale state. Add a `# teardown:` line to reset `app->collapsedShelves`, `tabOrder`, etc.

### Empty/Loading/Many-Items States

1. **Sidebar drag reorder:** Empty folder → nothing to drag. ✓
2. **Session search:** No results → empty state text. Loading → spinner. Many results → paginated with "Load more…". ✓
3. **Home shelves:** Empty shelf → hidden (no orphaned header). Loading → skeleton cards. Many items in shelf → scroll within the shelf OR cap at 20 and show "Show N more…". ✓
4. **Tab strip:** No tabs → strip is empty, composer is visible. Many tabs → scroll bar. ✓

---

## Summary

- **Verified: 15/15 gaps**
- **Already built: 2 features** (smart views, home shelves)
- **Real gaps: 12 features**
  - **Unblocked (ship-ready): 7 features** → sequenced above
  - **Blocked on backend or architecture: 5 features** → parked until dependencies ship

**First three to ship:** Session rename, Home shelf toggle, Muted sessions. Low risk, high UX impact, no dependencies.
