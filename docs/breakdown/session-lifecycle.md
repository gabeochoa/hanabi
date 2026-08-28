# Session Lifecycle Implementation Breakdown

**Verification Status (as of 2026-08-23)**

Of the 7 SESSION LIFECYCLE gaps in puffin_gaps.md:
- **4 verified as genuinely missing** from hanabi source
- **2 partially built** (archive state exists but incomplete UI)
- **1 explicitly blocked** (delete has no server verb)

Total gaps to address: **4 real, 2 partial.** See below for details.

---

## Gap Verification

### 1. Session rename ✓ Missing
- **Gap source claim**: Right-click a session → "Rename…" modal → send to server → durable echo updates display
- **Hanabi verify**: Grep for "rename" found only `disk_cache.h` comments and `auto_archive_days` setting. No rename UI, no wire call.
- **Backend support**: ✓ Available. The reference client's agentcloud-asks.md (§6) confirms `rename_v1` is announced on every attach. Wire shape: `{"cmd":"rename","title":"…"}` on the session subscription.
- **Status**: **SHIPPED** (branch `feat/session-rename`). Two corrections to the
  verification above, found by grepping before building: the live-frame
  classifier ALREADY recognised `session_renamed` (it maps to a
  `TitleUpdate` stream event during a turn — `agentcloud_client.cpp`), and the
  tab strip ALREADY had a right-click context menu (`TabStripComponent::menuOpen`
  and `render_tab_menu`), so 1a was an addition to an existing menu on the tab
  side and new only on the sidebar side.

### 2. Session fork (/btw) ✓ Shipped
- **Gap source claim**: `/btw Why did X fail?` creates new session with title "BTW: Why did X fail?" and opens in new tab
- **Hanabi verify**: `Client` exposes bare fork and atomic fork-with-prompt; Agentcloud encodes the documented control commands, and the mock preserves the source transcript.
- **Backend support**: ✓ `fork` and `fork_with_prompt`; `/btw` checks `fork_with_prompt_v1` before sending.
- **Status**: **SHIPPED** — see `docs/session-lifecycle-next.md`.

### 3. Session archive ⚠ Partially built
- **Gap source claim**: Right-click → "Archive" removes from main list, moves to "Archived" shelf. Undo toast. Can unarchive. Server-synced via InboxState API.
- **Hanabi verify**: 
  - Archive STATE: Settings.h has `get_auto_archive_days()` but that is LOCAL automatic archiving after N days, not manual per-session archive.
  - Archive UI: sidebar_system.h renders "Archived" view (Smart View with count badge) and the archive icon exists in the atlas.
  - Archive STATE handling: thread_model.h has `in_archived_view()` that filters ThreadState::Archived rows.
  - Archive TOGGLE: No code found for right-click → archive toggle, no undo toast.
- **Backend support**: agentcloud-asks.md (§5, correction 2026-08-10 and later) says archive is a per-viewer overlay persisted in `/api/inbox/state` (InboxState API) but Puffin cannot reach that route (gated on intern_oauth_token cookie). Puffin keeps a LOCAL archive in defaults (`ArchivedSessions`). Hanabi would need the same local-only approach OR need to wire the backend call if available.
- **Status**: **PARTIAL — view exists, toggle+undo not built, backend support unclear**

### 4. Session pin (star icon) ✓ Missing
- **Gap source claim**: Click star on sidebar row → pin/unpin. Pinned sessions sort to top. Icon is filled (★) when pinned, hollow (☆) when not. Per-viewer state, durable.
- **Hanabi verify**: Grep for "starred\|pin\|star" found only sidebar_system.h comments referencing starred in Smart Views (Starred view exists as a filter), but no per-session star toggle UI, no storage.
- **Backend support**: ? Unknown. The reference client's agentcloud-asks.md (§5) groups "archive and star and pin" as three separate machine-local sets that should become server state, but says this is lowest priority and archive/star/pin all need the same backend route (session overlay API). The route exists for archive but may not exist for star.
- **Status**: **REAL GAP** (backend support uncertain; treated as needing per-viewer sync)

### 5. Session mute (bell icon) ✓ Missing
- **Gap source claim**: Click bell on sidebar row → mute/unmute. Muted sessions don't trigger notifications. Machine-local state, not synced.
- **Hanabi verify**: No mute UI, no per-session mute state store, no notification gate.
- **Backend support**: ✓ Not needed (explicitly machine-local). Notification infrastructure exists (Settings.h has `get_notification_sound()` toggle).
- **Status**: **REAL GAP**

### 6. Sub-agent list with status panel ⚠ Partially built
- **Gap source claim**: Below the composer in main pane, a collapsible "Sub-Agents" section lists children with status (dot: blue=running, grey=done), title, last activity time. Click to open. Closed by default.
- **Hanabi verify**:
  - Sub-agents ARE shown in the sidebar (sidebar_system.h renders sub-agent rows; components.h has SessionSummary which carries children).
  - Sub-agents in MAIN PANE: no code found for a dedicated panel below the composer.
  - Status DOT: thread_model.h has glyph mapping for session states (active/idle/archived/parked) but applies to top-level sessions, not sub-agents.
- **Backend support**: ✓ Partially. hello.state.children is on the wire (agentcloud-asks.md §12 table), but per-child status requires looking up the child's own catalog row (workaround mentioned in §3 of asks).
- **Status**: **SHIPPED** — the transcript rollup remains, and the persisted
  sidebar panel loads the bounded real child catalog only while open.

### 7. Delete session ✗ Blocked
- **Gap source claim**: Right-click → "Delete" removes session server-side. No undo.
- **Hanabi verify**: No delete UI, no delete wire call.
- **Backend support**: ✗ BLOCKED. agentcloud-asks.md explicitly says "Delete. Verified absent across the tree, and still absent. Puffin's session menu deliberately omits it." No server verb exists.
- **Status**: **BLOCKED — no backend support**

---

## Work List: Real Gaps Only

The following sections describe the 4 real gaps + 2 partial gaps that can be built independently.

---

## Gap 1: Session Rename

**Status: shipped.** `Client::rename_session` returns the title the SERVER
settled on (agentcloud reads it off the durable `session_renamed` frame; the
mock echoes a trimmed copy and refuses an empty or over-long one), the loader
applies only that echo, and the modal stays up with the refusal when there is
one. Sidebar row: right-click → "Rename…" (offered only when
`supports_rename()`); tab: the same item on the existing tab menu. Proven by
`tests/ui/session_rename.e2e` (rename + refusal) and the
`fold_session_renamed` unit tests in `tests/unit/test_agentcloud.cpp`. Not
verified against the real orchestrator — no credentials here, and a rename is
a mutation.

**What ships**: Right-click a session in the sidebar or tab to get a context menu with "Rename…". A modal dialog appears with the current title. User edits and presses Return. Title is sent to server via `{"cmd":"rename","title":"…"}`. Durable echo (`session_renamed` frame type) updates the sidebar row and tab title. Server validation refusal shows an error message in the modal (no local optimism — must wait for echo).

**UX flow**:
1. Right-click sidebar row or tab title
2. Context menu shows "Rename" option
3. Click → modal dialog opens with current title selected
4. User types new title (or clears to use auto-derived title, if supported)
5. Press Return: modal closes, spinner appears on the row
6. Success: title updates in sidebar and all open tabs showing that session
7. Server refusal: modal re-opens with error message, title unchanged

**Depends on**:
- InputAction enum extension (add `SessionRenameMenu`, `SessionRenameConfirm`)
- Context menu system in sidebar_system.h and tab_bar_system.h (may already exist)
- Modal UI component (text input, confirm/cancel buttons)
- Wire call routing to agentcloud client's `run_turn()` with the rename command
- Frame parsing in agentcloud client to detect `session_renamed` durable event and dispatch to UI

**Where it goes**:
- **UI**: sidebar_system.h (context menu and durable title update on sidebar row), tab_bar_system.h (title update on tab), components.h (add rename-in-flight state)
- **Wire**: agentcloud_client.cpp (already handles commands on the subscription; route the rename frame)
- **Modal**: new or existing modal component in afterhours ui layer

**Proven by**:
- E2E test: `tests/ui/session_rename.e2e` — right-click a row, type new title, press Return, assert sidebar title changes and tab title changes
- Unit test: mock client should echo back a `session_renamed` frame; parser should update the session title

**Chunks** (each independently shippable):

### 1a. Context menu plumbing — sidebar row
Modal open/close state management and right-click menu on sidebar rows. Menu items: Rename, Archive (from partial gap #3). ~50 lines.

**UX**: Right-click sidebar row → context menu appears near cursor. Items are Rename, Archive (greyed if already archived). Click Rename or click away to close.

**Where**: sidebar_system.h render function, add menu component on long-press or right-click event. Add component state for menu visibility and position.

**Depends on**: InputAction for right-click (may need to extend input_mapping.h if not available).

**Proven by**: E2E: right-click a sidebar row, assert menu appears near cursor. Click away, assert menu closes.

### 1b. Rename modal UI
Modal dialog with text input, current title pre-filled and selected, confirm/cancel buttons, error message display. ~70 lines.

**UX**: Modal appears on top of main pane, dimmed background. Title field is focused, text selected. User types to replace. Return confirms, Escape cancels. During send, a spinner replaces the confirm button. On error, message appears below the field in red.

**Where**: new modal component in afterhours UI layer, or reuse existing modal framework if present. Integrate with ecs/components.h for modal state.

**Depends on**: Nothing blocking; modal pattern likely exists elsewhere in hanabi.

**Proven by**: Unit test: render modal with pre-filled title, assert text is selected. Simulate Return key, assert modal closes. Simulate error state, assert error message appears.

### 1c. Wire call and frame handling
Send rename command on the session subscription and handle the durable `session_renamed` event frame.

**UX**: After confirm button is pressed, spinner shows until server replies. On success, modal closes and sidebar/tabs update automatically.

**Where**: 
- agentcloud_client.cpp: handle `session_renamed` durable event (similar to how other durable events are folded)
- components.h: add a rename-in-flight flag on the session to gate the spinner
- agentcloud_client.h: document that `session_renamed` is now handled (update the comment about "THIS SLICE IMPLEMENTS list_sessions() AND NOTHING ELSE")

**Depends on**: agentcloud client's existing frame-folding infrastructure; agentcloud_auth.h already handles attach.

**Blocked on**: Backend; if rename_v1 is not announced in hello.capabilities, the UI should not offer the menu item (gate on capability check in sidebar render).

**Proven by**: E2E: mock client is updated to return a `session_renamed` frame after rename command received. Assert frame is parsed and title updates. Real backend: send rename to a test session, assert frame arrives and title updates.

---

## Gap 2: Session Fork (/btw)

**What ships**: `/btw <question>` slash command forks the thread. A new session is created with title "BTW: <question>" (or derived). The session opens in a new tab. Parent/child relationship is established (the fork origin is marked in the original transcript and the child knows its parent).

**UX flow**:
1. User types `/btw Why did the model refuse?` in the composer
2. Composer recognizes `/btw` prefix and shows a hint or command menu
3. User presses Return to send
4. Network request: fork command is sent to the server
5. Server creates a new session with the forked-from session as parent and the question as the initial prompt
6. New session opens in a new tab (or split pane, if split is enabled)
7. Original transcript shows a fork boundary marker (thin line, caption "Forked to [title]")
8. New tab shows the child session's transcript with the question as the first user message
9. On error: error message appears in the composer (refusal notice); no new tab opens

**Depends on**:
- Slash command parser in composer_system.h (if not already present)
- Fork wire verb on agentcloud client (may be a separate command or part of create_session)
- Tab or split-pane opening logic (already exists for other operations)
- Fork boundary marker rendering in main_pane_system.h (shows between messages)
- Child-session tracking in the session model (parent_id field, children list)

**Where it goes**:
- **Composer**: composer_system.h (slash command parsing, `/btw` prefix detection)
- **Wire**: agentcloud_client.cpp (fork command, likely `{"cmd":"fork_session","prompt":"…"}` or similar)
- **Tabs**: tab_model.h, tab_bar_system.h (open new tab for the forked session)
- **Transcript**: main_pane_system.h (render fork boundary marker)
- **Data model**: components.h, thread_model.h (add parent_id, track child relationship)

**Proven by**:
- E2E test: type `/btw test` in composer, press Return, assert new tab opens with forked session and original shows fork boundary
- Mock client: return a new session with forked_from set and parent relationship established
- Real backend: test against a real session, verify child session is created and visible in sidebar

**Chunks** (each independently shippable):

### 2a. Slash command parser in composer
Recognize `/btw <text>` at the start of the composer input and parse out the question. Show a hint or menu. ~60 lines.

**UX**: User types `/btw `. Composer shows a hint "Forks this conversation with the question: ". User continues typing the question. On Return, the question is extracted.

**Where**: composer_system.h, in the input render/update logic. Parse the text on each keystroke to detect `/btw` prefix.

**Depends on**: Nothing; uses existing composer text input.

**Proven by**: Unit test: parse `/btw Why?` and extract `Why?`. Parse `/btw ` with no text and extract empty. Parse `/ btw` (space after slash) and do not match.

### 2b. Fork wire call and tab opening
Send fork command to the server and open a new tab when the new session is created.

**UX**: After Return is pressed, spinner shows in composer. On success, a new tab opens and the session switches to it (or a notification appears saying "Fork created in new tab"). On error, error message appears in the composer.

**Where**:
- agentcloud_client.cpp: add fork command sending (route to run_turn with the fork command payload)
- loader_system.h or tab model: handle the new session creation and open a tab
- components.h: add a fork-in-flight state on the session

**Depends on**: Backend support for the fork verb (assumed available based on agentcloud-asks.md); tab opening logic (likely exists).

**Blocked on**: Confirmation that the fork verb exists on the server and its exact shape. Reference: agentcloud-asks.md (§6) says rename and fork are both "answered" and on the wire, but the exact fork payload is not documented. May need to ask the agentcloud oncall or read the Rust proto.

**Proven by**: E2E: send `/btw` command, assert new tab opens and contains the forked session. Mock client: return a new session with forked_from set.

### 2c. Fork boundary marker in transcript
Render a visual marker at the point in the original session's transcript where the fork occurred. Marker shows the title of the forked session and is clickable to open it.

**UX**: Original transcript shows a thin horizontal line (or a row-like divider) with text "Forked to: [child session title]". Clicking it opens the child session in a new tab.

**Where**: main_pane_system.h, in the row rendering loop. Add a marker row type when a frame indicates a fork boundary (or derive it from the session's children list and the turn count).

**Depends on**: Session's child list being available in the data model (components.h SessionChild or similar); fork boundary frame from the server (or client-side derivation).

**Proven by**: E2E: create a fork, assert a divider appears in the original transcript below the fork point. Click it, assert the child session opens.

---

## Gap 3: Session Archive (Partial)

**What ships**: Right-click a session in the sidebar to get "Archive" (or left-click a bell icon). Session disappears from main list and appears in the "Archived" Smart View. Can unarchive from the Archived view (same affordance). Toast undo bar appears after archive (10 seconds, "Undo" button).

**Status quo in hanabi**: 
- Archive view exists (smart view renders, shows only ThreadState::Archived sessions)
- Archive toggle UI: NOT BUILT
- Undo toast: NOT BUILT
- Backend: unclear; may be local-only or may have server sync (agentcloud-asks.md suggests both local and server options exist)

**UX flow**:
1. Right-click sidebar row or tab
2. Context menu shows "Archive" option
3. Click → session disappears from Recent/other views, appears in Archived shelf
4. Toast bar appears at bottom: "Archived. [Undo]" (10-second auto-dismiss)
5. Click Undo: session reappears in its original position
6. Close toast: no action needed
7. On success: toast dismisses, state is durable (survives app restart)

**Depends on**:
- Context menu (shared with rename, chunk 1a)
- Archive state storage (Settings or dedicated store like puffin's `ArchivedSessions`)
- Toast notification system (may exist in afterhours UI or need to be built)
- Backend sync (optional; can start with local-only like puffin)

**Where it goes**:
- **UI**: sidebar_system.h (menu item, update row visibility on toggle), components.h (add archive state)
- **State**: Settings.h or new ArchivedSessions store (similar to Settings pattern)
- **Toast**: new toast component, or extend existing notification system
- **Wire** (optional): agentcloud_client if backend sync is available

**Proven by**:
- E2E test: archive a session, assert it disappears from recent, appears in Archived. Click Undo, assert it reappears.
- Unit test: toggle archive state, assert it persists across app restart (mock by saving and reloading settings)

**Chunks** (reuses chunk 1a for context menu):

### 3a. Archive toggle in context menu (part of 1a)
Included in chunk 1a's context menu. Menu item: "Archive" (or "Unarchive" if already archived). ~20 lines (added to 1a's menu rendering).

### 3b. Toast notification system
A reusable toast component that shows a message with an optional action button at the bottom of the screen, auto-dismisses after N seconds, and can be manually dismissed. ~80 lines.

**UX**: Toast slides in from bottom, shows message and "Undo" button (or other action). Auto-dismisses after 10 seconds with a fade-out. Click the button to trigger the action. Close button (×) on the right.

**Where**: new toast system component in afterhours UI layer. Add to layout_system.h for positioning. Add component state for active toasts (could be a queue).

**Depends on**: afterhours animation and layout systems (likely already available).

**Proven by**: Unit test: create a toast, assert it appears. Simulate 10 seconds, assert it fades. Click action button, assert callback fires.

### 3c. Archive state storage
Persist the set of archived session IDs using Settings or a new dedicated store. Load on app launch. Update on toggle.

**UX**: No visible change; state is read/written silently.

**Where**: Settings.h (add get/set archived_sessions) or new ArchivedSessions singleton (similar to puffin).

**Depends on**: Settings file I/O already working.

**Proven by**: Unit test: archive a session, restart app (mock by calling load), assert session is still archived.

### 3d. Sidebar row visibility based on archive state
Filter sidebar rows based on archive state. Show in Recent/other views only if not archived. Show in Archived view only if archived.

**UX**: Archived sessions vanish from main sidebar, reappear in Archived Smart View.

**Where**: sidebar_system.h, in the render_sessions_list function. Add filter check: `if (s.is_archived && view != SmartView::Archived) continue;`.

**Depends on**: Chunk 3c (archive state available). Chunk 1a (context menu exists to trigger toggle).

**Proven by**: E2E: archive a session, assert it leaves the Recent view and appears in Archived.

### 3e. Undo toast action
Connect the toast's action button to reverse the archive toggle.

**UX**: Click Undo on the toast → session is unarchived immediately, toast dismisses.

**Where**: sidebar_system.h or a dedicated archive-action handler. On click, toggle archive state, refresh sidebar render, dismiss toast.

**Depends on**: Chunks 3b, 3c, 3d.

**Proven by**: E2E: archive, click Undo on toast, assert session reappears immediately.

---

## Gap 4: Session Pin (Star Icon)

**What ships**: Click a star icon on a sidebar row to pin/unpin. Pinned sessions sort to the top of their view (or stay in place, depending on design). Icon is filled (★) when pinned, hollow (☆) when not. Per-viewer state, durable (survives app restart). Separate from "keep tab open" state.

**UX flow**:
1. Hover over sidebar row
2. Star icon appears (or is always visible)
3. Click star → icon fills, session moves to top (or stays in place with a visual marker)
4. Click again → icon becomes hollow, session returns to normal order
5. On success: state is durable (persists across tabs, windows, and app restart)

**Depends on**:
- Pin state storage (similar to archive)
- Icon rendering (lucide star icon in the atlas, or code to draw filled/unfilled)
- Sort override logic in sidebar rendering
- Server sync (optional, but needed for multi-machine support)

**Where it goes**:
- **UI**: sidebar_system.h (render star icon, toggle on click, visual feedback)
- **State**: Settings.h or new StarredSessions store (similar to archive)
- **Sort**: sidebar_system.h (sort pinned to top within each view)
- **Wire** (optional): agentcloud_client if backend sync is available

**Proven by**:
- E2E test: click star on a session, assert icon fills and session moves to top. Click again, assert icon becomes hollow and session returns to normal order.
- Unit test: pin a session, restart app, assert pin state persists.

**Chunks** (each independently shippable):

### 4a. Star icon rendering and click handling
Render a star icon on sidebar rows (filled or hollow based on pin state). Toggle pin state on click. ~40 lines.

**UX**: Star icon appears on the right side of each sidebar row (or left, depending on design). Filled (★) if pinned, hollow (☆) if not. Click to toggle.

**Where**: sidebar_system.h, in render_chat_row. Add a clickable star icon. On click, toggle pin state (connect to chunk 4b).

**Depends on**: Icon available in the atlas (check if star icon exists; if not, add or use Unicode ★/☆).

**Proven by**: E2E: click star on a row, assert icon changes to filled. Click again, assert icon becomes hollow.

### 4b. Pin state storage
Persist the set of pinned session IDs. Load on app launch.

**UX**: No visible change; state is read/written silently.

**Where**: Settings.h (add get/set pinned_sessions) or new PinnedSessions singleton.

**Depends on**: Settings file I/O already working.

**Proven by**: Unit test: pin a session, restart app, assert pin state persists.

### 4c. Sort pinned sessions to top
Reorder sidebar rows so pinned sessions appear at the top of their view.

**UX**: After clicking star, the session jumps to the top of the Recent/other views (or Pinned view, if a dedicated view exists).

**Where**: sidebar_system.h, in render_sessions_list. Add a sort key: pin state (pinned first), then original order (age).

**Depends on**: Chunk 4b (pin state available). Chunk 4a (toggle works).

**Proven by**: E2E: pin three sessions in different orders, assert they all move to the top and maintain pin-order.

---

## Gap 5: Session Mute (Bell Icon) — Machine-Local

**What ships**: Click a bell icon on a sidebar row to mute/unmute. Muted sessions don't trigger notifications. Icon is crossed out (🔇) when muted, normal (🔔) when not. Machine-local state, not synced. Muting a parent does not mute children.

**UX flow**:
1. Hover over sidebar row
2. Bell icon appears (or is always visible)
3. Click bell → icon becomes crossed out (🔇)
4. When that session receives a message, no notification is triggered
5. Click again → icon becomes normal (🔔), notifications resume
6. State persists across app restart, tabs, and windows (on this machine only)

**Depends on**:
- Mute state storage (Settings or new MutedSessions store)
- Icon rendering (bell icon in atlas, drawn or unicode 🔔/🔇)
- Notification gate (check mute state before firing notification)
- Icon click handling (same pattern as archive toggle)

**Where it goes**:
- **UI**: sidebar_system.h (render bell icon, toggle on click)
- **State**: Settings.h or new MutedSessions store
- **Notification gate**: wherever notifications are fired (likely event_subscription or a notification handler)

**Proven by**:
- E2E test: mute a session, simulate a message arrival, assert no notification fires. Unmute, simulate a message, assert notification fires.
- Unit test: mute a session, restart app, assert mute state persists.

**Chunks** (each independently shippable):

### 5a. Bell icon rendering and click handling
Render a bell icon on sidebar rows (normal 🔔 or crossed 🔇 based on mute state). Toggle mute state on click. ~40 lines.

**UX**: Bell icon appears on the right side of each sidebar row (or near the star, if star is implemented). Normal bell (🔔) if not muted, crossed bell (🔇) if muted. Click to toggle.

**Where**: sidebar_system.h, in render_chat_row. Add a clickable bell icon. On click, toggle mute state (connect to chunk 5b).

**Depends on**: Bell icon in atlas (or unicode). Icon rendering and click routing.

**Proven by**: E2E: click bell on a row, assert icon changes. Click again, assert it changes back.

### 5b. Mute state storage
Persist the set of muted session IDs. Load on app launch. Update on toggle.

**UX**: No visible change; state is read/written silently.

**Where**: Settings.h (add get/set muted_sessions) or new MutedSessions singleton.

**Depends on**: Settings file I/O already working.

**Proven by**: Unit test: mute a session, restart app, assert mute state persists.

### 5c. Notification gate
Before firing a notification for a session, check if it is muted. Skip notification if muted.

**UX**: Muted sessions never trigger notifications, even if they receive messages.

**Where**: wherever notifications are fired (likely event handling code or a dedicated notification system). Add check: `if (is_muted(session_id)) return;` before firing.

**Depends on**: Chunk 5b (mute state available). Notification system (must exist; hanabi has Settings::get_notification_sound()).

**Proven by**: E2E: mute a session, send a message, assert no notification. Real or mock: verify the check is in place before notification dispatch.

---

## Gap 6: Sub-Agent Panel in Main Pane (Partial)

**What ships**: Below the composer in the main pane (above the status bar or in a collapsible section), a "Sub-Agents" panel lists all child sessions with:
- Status dot (blue = running, grey = done)
- Child session title
- Last activity time (e.g., "4m ago")
- Click to open the child in a new tab

Closed by default; toggle to expand/collapse. State persists (which panels are expanded).

**Status quo in hanabi**: 
- Sub-agents ARE listed in the sidebar (each child is a separate sidebar row)
- Sub-agents NOT in main pane (no dedicated panel)
- Status tracking: partial (catalog row join is required; not yet implemented)

**UX flow**:
1. User opens a session with sub-agents
2. Below the composer, a collapsed "Sub-Agents" section appears (if children exist)
3. Click to expand
4. List of children appears: each shows a status dot, title, and time
5. Blue dot = child is running; grey dot = child is done
6. Click a child → it opens in a new tab
7. Close the app and reopen; panel remains expanded/collapsed as before

**Depends on**:
- Sub-agents list in the data model (components.h already has SessionChild, carried on hello.state.children)
- Status tracking (requires catalog row lookup or server-provided status in the projection)
- Panel UI component (collapsible section with a title and list of rows)
- Expand/collapse state persistence (Settings or component-local state)
- Child opening logic (new tab, same as fork)

**Where it goes**:
- **Data model**: components.h (may need to add status field to SessionChild if server provides it; otherwise, join against catalog)
- **UI**: main_pane_system.h (render panel below composer, before status bar)
- **State**: Settings.h (add expand/collapse state for each session)
- **Catalog join** (optional): loader_system.h (fetch child catalog rows to get status)

**Proven by**:
- E2E test: open a session with sub-agents, assert panel appears. Click to expand, assert list shows. Click a child, assert new tab opens with that session.
- Mock client: return session with children in hello.state.children; return catalog rows with status for each child.

**Chunks** (some depend on partial infra):

### 6a. Sub-agent list data model
Ensure SessionChild in components.h has all fields needed (id, title, status from catalog or projection). Implement catalog join if status is not in the projection.

**UX**: No visible change; data is available internally.

**Where**: components.h (define SessionChild if not complete), loader_system.h (join child sessions against catalog to get status, if needed).

**Depends on**: agentcloud client's session list and catalog fetch logic.

**Blocked on**: Confirmation whether status is on the projection or requires catalog join. agentcloud-asks.md (§3) says status requires a workaround via catalog join. This chunk covers that workaround.

**Proven by**: Unit test: load a session with children, assert each child has status (or mock the catalog join).

### 6b. Sub-agent panel UI component
Render a collapsible "Sub-Agents" section below the composer, showing the list of children with status dots, titles, and times.

**UX**: Section header: "Sub-Agents: N running" (or "Sub-Agents (closed)" if collapsed). Click header to toggle. When open, list of children with blue/grey dots, titles, and times (e.g., "4m ago").

**Where**: main_pane_system.h, in the render function. Add a new section after the transcript and before the status bar. Use afterhours collapsible component.

**Depends on**: Chunk 6a (child data available). afterhours collapsible component (likely exists).

**Proven by**: E2E: render the panel, assert it appears below composer. Click to expand/collapse, assert it toggles.

### 6c. Expand/collapse state persistence
Persist which sessions have the sub-agent panel expanded. Load on app launch.

**UX**: Open a session, expand the sub-agent panel. Close the app and reopen; panel is still expanded (for that session).

**Where**: Settings.h (add a set of session IDs whose sub-agent panels are expanded) or component state (simpler, but state is lost on app restart — not acceptable per the spec).

**Depends on**: Settings I/O (already working).

**Proven by**: Unit test: expand panel, save settings, load, assert panel is still marked as expanded.

### 6d. Child session click handling
Click a child in the list to open it in a new tab.

**UX**: Click a child session row → new tab opens with that child session.

**Where**: main_pane_system.h, in the child row render. On click, route to tab opening logic (similar to fork chunk 2b).

**Depends on**: Tab opening logic (likely exists). Chunk 6b (child rows render).

**Proven by**: E2E: click a sub-agent in the panel, assert new tab opens with that session.

---

## Summary: Chunks by Shipping Order

**Tier 1 — Session Rename (highest value, simple, no dependencies)**

1. **1a. Context menu plumbing** (50 lines, handles both rename and archive)
2. **1b. Rename modal UI** (70 lines, standalone)
3. **1c. Rename wire call and frame handling** (80 lines, depends on 1a + 1b)

**Tier 2 — Session Archive (enhances navigation, reuses chunk 1a)**

4. **3b. Toast notification system** (80 lines, reusable)
5. **3c. Archive state storage** (30 lines, standalone)
6. **3d. Sidebar visibility filtering** (20 lines, integrates with 1a + 3c)
7. **3e. Undo action** (20 lines, integrates with 3b + 3d)

**Tier 3 — Session Mute (simple, local-only)**

8. **5a. Bell icon and toggle** (40 lines, similar to chunk 4a)
9. **5b. Mute state storage** (30 lines, similar to 3c)
10. **5c. Notification gate** (20 lines, minimal, integrates existing notify path)

**Tier 4 — Session Fork and Sub-Agent Panel (higher complexity, backend-dependent)**

11. **2a. Slash command parser** (60 lines, composer-only)
12. **2b. Fork wire call and tab opening** (100 lines, depends on backend verb shape)
13. **2c. Fork boundary marker** (50 lines, depends on 2b + frame presence)
14. **6a. Sub-agent data model** (TBD, may be 0 if status is on projection)
15. **6b. Sub-agent panel UI** (100 lines, depends on 6a)
16. **6c. Panel expand/collapse persistence** (30 lines, depends on 6b)
17. **6d. Child click handling** (20 lines, depends on 6b)

**Tier 5 — Session Pin (polish, similar complexity to archive)**

18. **4a. Star icon rendering** (40 lines, similar to 5a)
19. **4b. Pin state storage** (30 lines, similar to 5b)
20. **4c. Sort to top** (30 lines, integrates with sidebar render)

---

## Three Chunks to Ship First (Minimum Viable Session Lifecycle)

1. **Chunk 1a + 1b + 1c — Session rename** (200 total)  
   *Why*: Core workflow every user does. Small, no blockers. Enables session organization immediately.

2. **Chunk 3b + 3c + 3d + 3e — Session archive with undo** (150 total)  
   *Why*: Reuses context menu from chunk 1a. Helps users file away old conversations. Toast UX is polished.

3. **Chunk 2a + 2b — Session fork (slash command + wire)** (160 total)  
   *Why*: Core debugging workflow (branch out to test a hypothesis). High user impact. Blocks nothing else.

---

## Backend Verification Needed (Before Shipping)

| Gap | Wire Verb | Status | Action |
|-----|-----------|--------|--------|
| Rename | `{"cmd":"rename","title":"…"}` | ✓ Confirmed in reference client docs (agentcloud-asks.md §6) | Proceed |
| Fork | `/btw` or similar | ? Assumed available; shape unknown | Confirm exact payload shape with agentcloud oncall or read Rust proto |
| Archive | Per-viewer overlay via inbox_state API | ⚠ Route is gated on intern_oauth_token (not available to Puffin locally). Puffin uses local storage. | Start with local-only storage; backend sync is optional follow-up |
| Pin | Same as Archive | ⚠ Same status | Start with local-only storage |
| Mute | N/A (local-only) | — | — |
| Sub-agent status | Status on projection or catalog join | ⚠ agentcloud-asks.md (§3) says requires catalog join | Implement catalog join workaround (fetch child catalog rows) |
| Delete | (no verb) | ✗ Verified absent | Skip this gap entirely |

---

## Test Plan Outline

**E2E tests** (under `tests/ui/*.e2e`, drive the real UI):
- `session_rename.e2e`: right-click, rename, assert sidebar + tab title update
- `session_archive.e2e`: right-click, archive, assert row disappears from Recent + appears in Archived. Click Undo, assert reappears.
- `session_mute.e2e`: click bell, send message, assert no notification. Unmute, send, assert notification fires.
- `session_fork.e2e`: type `/btw test`, assert new tab opens with "BTW: test" title and fork boundary marker appears in original transcript.
- `session_pin.e2e`: click star, assert icon fills + session moves to top of list.
- `subagent_panel.e2e`: open a session with children, expand sub-agent panel, click a child, assert new tab opens.

**Unit tests** (under `tests/unit/`):
- State persistence: save and load each state type (archive, mute, pin, expand/collapse), assert consistency.
- Wire parsing: mock client returns frames for each operation, assert parsing is correct.
- Input validation: rename with empty/too-long title, fork with no text, etc.; assert graceful handling.

---

## Constraints & Unknowns

**Constraints**:
- No parent-company names, internal hostnames, service names, or internal paths in documentation.
- Comments in hanabi code stay to one line.
- Lazy engineering (ponytail): reuse existing components, don't over-engineer.

**Unknowns**:
- Exact payload shape for fork (`/btw`) command — need to confirm with backend team or read Rust proto.
- Whether archive/pin/mute have server-side equivalents or are machine-local only. Start local; backend sync is optional.
- Whether sub-agent status is on the projection or requires catalog join. agentcloud-asks.md suggests join; implement that.
- Whether afterhours UI already has toast/modal/collapsible components, or if they need to be built.

**Blockers**:
- Delete: no server verb; explicitly skip this gap.
- Fork: exact wire shape unknown; confirm before shipping (chunk 2b).
- Sub-agent status: may require catalog join; verify data availability (chunk 6a).

---

## Files to Create / Modify

**New files** (if not reusing existing modals/toasts):
- (None required; reuse existing afterhours components where possible)

**Modified files**:
- `src/ecs/sidebar_system.h` — add context menu, star/bell icons, archive filtering
- `src/ecs/main_pane_system.h` — add sub-agent panel, fork boundary marker
- `src/ecs/composer_system.h` — add slash command parser
- `src/ecs/components.h` — add rename-in-flight, fork-in-flight, sub-agent data
- `src/ecs/layout_system.h` — add toast positioning (if building toast)
- `src/api/agentcloud_client.cpp` — handle rename/fork frames, route commands
- `src/api/agentcloud_client.h` — document rename/fork support
- `src/settings.h` — add get/set for archive, mute, pin, expand/collapse state
- `tests/ui/*.e2e` — new E2E tests for each feature
- `tests/unit/*.cc` — unit tests for state persistence and parsing

---

## Implementation Notes (Lazy Engineering)

1. **Toast component**: Check if afterhours already has a notification or toast system. If yes, reuse. If no, implement a minimal version (~80 lines) with auto-dismiss and action callback.

2. **Modal component**: Check if afterhours has a modal or dialog system. If yes, reuse for rename. If no, a minimal text-input modal in the UI layer (~70 lines).

3. **Context menu**: Check if sidebar already routes right-click events. If yes, add menu items. If no, extend input_mapping.h with right-click action and implement menu routing.

4. **Collapsible component**: Check if afterhours has a collapsible/disclosure component (likely, given the UI complexity elsewhere). Reuse for sub-agent panel.

5. **Star/bell icons**: Use lucide icons from the existing atlas if available. If not, use Unicode characters (★/☆, 🔔/🔇) or add simple SVG paths.

6. **Sort logic**: Sort is done in-place during render; no separate sort data structure needed. Inline a comparator in sidebar_system.h's render function.

7. **Settings I/O**: Reuse the existing Settings::write_save_file() and load_save_file() pattern. Add JSON fields for each new state (archived_sessions, muted_sessions, etc.).

---

## Definition of Done

For each chunk, a PR is complete when:

1. **Code is written** (see sizes above; <250 lines per chunk).
2. **Tests pass** (E2E and unit; see Test Plan Outline).
3. **Verification**: On the real backend, the feature works as described (or on mock client if backend verb is unavailable).
4. **No new dependencies** added (use stdlib, afterhours, and existing hanabi patterns).
5. **One theme per PR** — if a change needs both "Fixed" and "Added" sections, split into two PRs.

---
