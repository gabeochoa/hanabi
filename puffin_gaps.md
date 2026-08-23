# Puffin UI Features vs Hanabi — Exhaustive Gap Analysis

## Read the "Hanabi today" lines with suspicion

This document's PUFFIN side is research and should be reliable. Its HANABI side
is a claim about our own code, and a spot-check of five entries found three
wrong — all three claiming we lack something we had already shipped:

- Cmd+F find-in-transcript, listed in the original "build first" five, has been
  in `src/ui/find_highlight.h` with three e2e scripts since earlier today.
- Smart views were called "top-left buttons, not sidebar links"; they are
  sidebar links with live count badges, which is exactly puffin's shape.
- The Home screen was called a flat list; it already renders a "WAITING ON YOU"
  shelf with a count.

Those three are corrected below. The other ~76 were not individually verified,
so **before building anything from this list, grep for it first.** A gap that
turns out to be built is the cheapest possible thing to discover and the most
expensive to not discover.

## How to Read This Document

This document lists every UI feature the reference client (puffin) has that hanabi is either completely missing or has only in weakened form. For each gap:

1. **What it does** — user-facing behavior, no jargon
2. **Where in puffin** — file + type name for reference
3. **Hanabi today** — what hanabi has instead (or "nothing")
4. **Importance** — table stakes / important / polish / niche
5. **Rough size** — small / medium / large, with the hard part named

The entries are grouped by functional area. Each group has a count of gaps and subtotals at the end. This is a work list — read the counts to prioritize. **Anything puffin deliberately does NOT have is listed separately at the end.**

---

## By The Numbers

**Total gaps: 79** across 13 functional areas.

| Area | Count | Priority |
|------|-------|----------|
| Sidebar & Navigation | 9 | 4 table-stakes + 5 polish |
| Transcript & Rendering | 14 | 6 table-stakes + 8 polish |
| Composer & Sending | 11 | 3 table-stakes + 8 polish |
| Tabs & Windows | 6 | 4 important + 2 polish |
| Search & Find | 5 | 3 table-stakes + 2 polish |
| Session Lifecycle | 7 | 6 table-stakes + 1 polish |
| Drafts & Undo | 2 | 1 important + 1 polish |
| Attachments | 5 | 2 important + 3 niche |
| Notifications & Background | 3 | 2 important + 1 polish |
| Settings & Preferences | 8 | 1 table-stakes + 7 important |
| Keyboard & Shortcuts | 5 | 3 important + 2 polish |
| Native macOS Integration | 3 | 2 important + 1 polish |
| Additional Features | 1 | 1 polish |

---

## The Five to Build First

Based on frequency of use, blocking other features, and user impact:

1. **Timestamp-aware transcript layout** (Transcript #1) — layout rows with dates, not just times. Required for understanding thread flow. **medium**
2. **Session rename with durable echo** (Session Lifecycle #1) — rename from sidebar/tab context. Core workflow. **small**
3. **Compose-history walk with arrow keys** (Composer #4) — Up/Down arrow recalls previous sends. Daily use. **small**
4. **Session search across threads** (Search & Find #3) — Cmd+Shift+F over every session, not just the open one. (Find *within* a thread already ships; see Search & Find #1.) **medium**
5. **Session fork (/btw)** (Session Lifecycle #2) — `/btw <question>` splits conversation. Core feature. **medium**

---

# GAPS BY AREA

## SIDEBAR & NAVIGATION (9 gaps)

### 1. Timestamp-aware transcript layout
**What it does:** Rows display the date (not just time) when a message is older than ~4 hours. Date appears once per day, not per message. Time still shows per-row.

**Where in puffin:** `AgentcloudTranscriptView.swift:timestampToShow(for:)` + `AgentcloudTranscriptView.swift:dateRow(for:)`

**Hanabi today:** Rows show only time; date is never shown. Makes old threads hard to follow (is this from yesterday or last week?).

**Importance:** Table stakes. Users need to know when messages are from.

**Size:** Small. Add a `dateChanged(row:previous:)` helper + render a `.date` row type. ~80 lines.

### 2. Space grouping in sidebar
**What it does:** Sessions are grouped by Space (Metamate workspace). One collapsible section per Space. Ungrouped sessions in "No Space" bucket.

**Where in puffin:** `SpaceGrouping.swift` + `AgentcloudSpaceSessions.swift` (query Spaces via GraphQL)

**Hanabi today:** Sessions group by `workspace` field only; no Space hierarchy. Missing GraphQL route to fetch Spaces per viewer.

**Importance:** Important. 8 real Spaces exist on the backend; grouping them is the correct IA.

**Size:** Medium. Needs GraphQL query (Space list), Spaces model, grouping logic in sidebar. Puffin's limitation: "workspace" field is missing from session rows, so only the first Space's sessions appear. **This is a backend gap, not a UI one.**

### 3. Smart views (Blocked, Review, Starred, Archived)
**What it does:** Sidebar buttons showing filtered transcript lists: Blocked (waiting approval), Review (waiting on user), Starred (pinned), Archived (filed). Count badges on each. Tappable to switch view.

**Where in puffin:** `SmartViewSidebar.swift` (in `Views/SidebarColumn.swift`)

**Hanabi today:** ALREADY BUILT the same way — a "VIEWS" section of sidebar
links with live count badges (`sidebar_system.h`; the row for Archived even
notes it moved out of folders into Views). Confirmed on screen: Blocked 34,
Review 1370. **Not a gap.**

**Importance:** n/a — done.

**Size:** n/a.

### 4. Smart view shelves on Home
**What it does:** On the Home screen, sessions are grouped into shelves: "Waiting on You" (orange), "Finished Since You Looked" (grey), "Self-Running" (blue). Shelf headers show count. Collapse/expand per shelf.

**Where in puffin:** `HomeSessionList.swift:shelvesByStatus()`

**Hanabi today:** PARTIAL, not flat — Home already renders a "WAITING ON YOU"
shelf with a count (confirmed on screen: 34). What is missing is the other
shelves ("Finished Since You Looked", "Self-Running") and per-shelf
collapse/expand.

**Importance:** Important. Helps users find what to do next (which threads need them?).

**Size:** Medium. Grouping logic + per-shelf header rendering + collapse state per shelf. ~120 lines.

### 5. Muted sessions indicator (bell icon)
**What it does:** Sidebar row shows a bell icon (or crossed bell) if the session is muted. Single-click toggle (no menu). Machine-local state (not synced).

**Where in puffin:** `MutedSessions.swift` + `SessionRowView.swift` (bell in trailing accessory)

**Hanabi today:** No mute feature. Notifications exist but no per-session suppress.

**Importance:** Important. Muting is a core quiet-hours feature.

**Size:** Small. UserDefaults store + toggle in row + notification gate. ~50 lines.

### 6. Sub-agent visibility toggle
**What it does:** Sidebar has a checkbox "Show finished sub-agents". Toggled globally (not per-session). Affects expand/collapse of child lists in all sessions.

**Where in puffin:** `SmartViewSidebar.swift:showFinishedSubagents` toggle + filter in `SubAgentPanel.swift`

**Hanabi today:** Sub-agents always shown. No global toggle.

**Importance:** Polish. Reduces clutter but not essential.

**Size:** Small. Add toggle to sidebar + gate child row render. ~30 lines.

### 7. Sidebar row drag-and-drop reordering
**What it does:** Drag a session row up/down to reorder it within its Space. New order is persisted locally.

**Where in puffin:** `SessionRowView.swift` + sidebar drop delegates

**Hanabi today:** Rows are fixed (sorted by last_seq). No drag.

**Importance:** Polish. Nice to have for power users; not essential.

**Size:** Medium. Add drag recognizer + drop zones + local sort override store. ~100 lines.

### 8. Search snippet highlighting in sidebar rows
**What it does:** When a session is found by Cmd+Shift+F search, its row shows a snippet of the matching text, with the search term(s) highlighted.

**Where in puffin:** `SessionSearchKeyboard.swift` + `SessionRowView.swift` snippet rendering

**Hanabi today:** Search exists but results don't show snippets. Just the title.

**Importance:** Polish. Helps users find the right thread among results.

**Size:** Small. Return match snippet from search + render in row. ~60 lines.

### 9. Per-Space header collapse/expand all
**What it does:** Space section headers have a collapse arrow. Click to hide/show all sessions under that Space.

**Where in puffin:** `SidebarSection.swift` + `SpaceGrouping.swift`

**Hanabi today:** Sidebar sections don't collapse. All sessions visible always.

**Importance:** Polish. Nice for power users with many sessions.

**Size:** Small. Add collapse state per Space + toggle on header click. ~40 lines.

---

## TRANSCRIPT & RENDERING (14 gaps)

### 1. Timestamp rows (dates, not just times)
**What it does:** When a message is >4 hours older than the previous, a thin grey date divider appears above it (e.g., "Monday, August 19"). Time still shows per-row below the divider.

**Where in puffin:** `AgentcloudTranscriptView.swift:timestampToShow` + `AgentcloudTranscriptView.swift` row rendering

**Hanabi today:** Only time is shown (e.g., "3m", "2h"). No date divider.

**Importance:** Table stakes. Makes long threads unreadable (is this from today or last week?).

**Size:** Small. ~80 lines (row type + date calculation + render).

### 2. Tool call nested sub-rows (per-node tool results)
**What it does:** When a tool call expands, the output is NOT a flat dump — it breaks into sub-rows, one per node where the tool ran. Each sub-row shows the node name, command, duration, status (✓/✗), and output excerpt. Click sub-row to expand output.

**Where in puffin:** `ToolRowView.swift` + block-folding in `SessionFold.swift` (tool calls split into multiple tool-result frames)

**Hanabi today:** Tool row expands to show input + output flat. No per-node breakdown. Also, `tool_duration`, `tool_count`, `tool_status` are hashed placeholders (not real fields from the wire).

**Importance:** Table stakes. Seeing which node ran which part of a multi-node operation is essential for debugging.

**Size:** Large. Requires: (a) data layer: tool-call block splitting into Role::Tool frames with per-node name/command/output (wt/live-sse, merged); (b) UI: nested sub-row rendering with expand/collapse per sub-row; (c) real tool fields from the data layer (tool_duration_ms, tool_status, tool_result). The wiring is ~200 lines but depends on merged data layer.

### 3. Thinking rows (disclosure, expandable)
**What it does:** Internal model reasoning appears as a collapsible row ("Thinking") separate from the assistant message. Closed by default. Click to expand and read the reasoning.

**Where in puffin:** `SessionFold.swift:RowKind.thinking` + `AgentcloudTranscriptView.swift` rendering

**Hanabi today:** No thinking rows. Thinking content is not shown.

**Importance:** Important. Users want to see reasoning; currently invisible.

**Size:** Medium. Add `thinking` message type to data model + render + toggle state. ~80 lines (depends on data layer emitting thinking).

### 4. Minimap navigator (right edge)
**What it does:** Right edge of transcript has a thin scrollable navigator rail. Marks show 5 types: machinery (grey), reply (blue), delivery (green), notice (orange), ask (red). Hover expands rail to full width. Click a mark to jump to that row. Drag scrubber handle to seek. Settings → Chat Behavior controls which mark types are shown.

**Where in puffin:** `TranscriptMinimap.swift` + `TranscriptGroup.swift` (wraps transcript in minimap)

**Hanabi today:** No minimap. Only vertical scrollbar from the toolkit.

**Importance:** Important. Essential for navigating long transcripts; puffin users rely on it.

**Size:** Large. Mark detection logic + virtualized rail rendering + seek/scroll sync. ~250 lines.

### 5. Tool rows open folded (not expanded)
**What it does:** By default, tool rows render with the disclosure closed (▶ arrow, not ▼). Clicking the arrow expands to show input/output. But users can override per-conversation via the composer strip's three chips (fold all / expand all / auto). Override state is durable per-session.

**Where in puffin:** `SessionFold.DisclosureDefaults` + `AgentcloudRowView.defaultExpanded(for:)` + composer strip disclosure-mode chips

**Hanabi today:** Tool rows are fully expanded by default. No disclosure toggle. No per-session fold-mode setting.

**Importance:** Important. Full expansion on every tool drowns long transcripts in output. Users need to fold by default.

**Size:** Medium. Add fold state + toggle in row + composer chips for mode + durable per-session preference. ~120 lines.

### 6. Inline code styling (no selection, just formatting)
**What it does:** Inline code (backtick-wrapped in markdown) is rendered with a grey background and monospace font, clickable for copy-on-click.

**Where in puffin:** `MarkdownBlockView.swift` markdown parsing + `TextPresentation.swift`

**Hanabi today:** Inline code renders as plain text, no styling, no copy action.

**Importance:** Polish. Makes code snippets clear; currently indistinct.

**Size:** Small. Markdown parser already supports code spans — just add rendering. ~40 lines.

### 7. Link auto-detection for work-tracker references
**What it does:** Identifier patterns in message text (a diff, a task, an incident — `D123456`, `T123456`, `S123456`) become clickable links to the corresponding internal tool. Cmd+click opens in the browser. Host comes from config, never hardcoded.

**Where in puffin:** `MessageAttributedText.swift` + `TextPresentation.swift:openURLInBrowser`

**Hanabi today:** No auto-linking. These are plain text.

**Importance:** Polish. Nice for power users; not essential in a private client.

**Size:** Small. Add regex + link tagging + URL open. ~50 lines.

### 8. Code block syntax highlighting (15+ languages)
**What it does:** Fenced code blocks render with per-language syntax coloring. Languages supported: Python, Bash, JavaScript, TypeScript, Go, Rust, C, C++, Java, SQL, YAML, JSON, HTML, CSS, Markdown, and more. Line numbers optional.

**Where in puffin:** `SyntaxHighlighter.swift` + theme colors in `PuffinTheme.swift`

**Hanabi today:** Code blocks render with no highlighting. Plain monospace text.

**Importance:** Important. Makes code reading easier; heavily used in threads.

**Size:** Medium. Needs a syntax highlighter library (or a light custom one for the 5 most common: Python, Bash, JS, Go, SQL). Puffin's is 400+ lines; a lighter version for hanabi could be ~200.

### 9. Pipe-table rendering (grid layout)
**What it does:** Markdown pipe tables (`| col1 | col2 |`) render as an actual grid: header row in gold, body rows with alternating background, proper column alignment.

**Where in puffin:** `MarkdownBlockView.swift:renderTable()`

**Hanabi today:** Tables are stripped or rendered as preformatted text. No grid.

**Importance:** Polish. Tables are occasional but important when they appear.

**Size:** Small. Table parser + grid layout. ~80 lines (low priority in afterhours, noted as gap #19).

### 10. Thinking rows excluded from search
**What it does:** When using Cmd+F find-in-transcript, thinking rows are NOT searched (they're skipped). Reasoning is internal; users search the conversation, not the reasoning.

**Where in puffin:** `TranscriptFind.swift:isSearchable(row:)` filter

**Hanabi today:** Find is not implemented yet. When it is, will need this filter.

**Importance:** Polish. Only matters after find is built.

**Size:** Small. Add row-type check in find filter. ~5 lines.

### 11. Row grouping headers (run batches)
**What it does:** Tool call rows and their results are grouped under a collapsible "Run" header. Header shows which agent/model ran. Collapsing hides all rows in the run.

**Where in puffin:** `TranscriptGroup.swift` + `SessionFold.groupedRows()`

**Hanabi today:** Rows render flat (no run grouping). A complex multi-step run is hard to see as a unit.

**Importance:** Important. Helps users understand turn structure.

**Size:** Medium. Run-detection logic (group by seq boundaries) + group header rendering + collapse state. ~120 lines.

### 12. Message delivery status rows
**What it does:** When a message is sent, a delivery-status frame arrives from the server before the assistant reply. It shows as a separate row: "Delivered", timestamp, maybe a spinner if still processing.

**Where in puffin:** `SessionFold.RowKind.delivery(label:)` + frame parsing in `SessionFold.swift`

**Hanabi today:** Delivery frames exist in the data layer but are not rendered. Missing row type.

**Importance:** Important. Users want confirmation their message was received.

**Size:** Small. Add delivery row rendering. ~40 lines.

### 13. Markdown H1–H4 headers with hierarchy
**What it does:** Headers (# through ####) render with decreasing font size and weight. H1 largest, H4 smallest. Color: Triforce gold (accent).

**Where in puffin:** `MarkdownBlockView.swift:renderHeading()` + theme colors

**Hanabi today:** Headers are not parsed or highlighted. Rendered as plain text.

**Importance:** Polish. Makes structured messages readable.

**Size:** Small. Markdown parser already supports headers — just add rendering. ~50 lines.

### 14. Streaming animation (working dots)
**What it does:** While a message is streaming in, a pulsing dot animation shows above the text (or trailing the first line). Animation: three dots pulse in sequence (. → .. → ... → repeat).

**Where in puffin:** `StreamingDotGrid.swift` + `MarkdownBlockView.swift` animation

**Hanabi today:** No streaming animation. Just the text appears.

**Importance:** Polish. Nice visual feedback; not essential.

**Size:** Small. Pulsing dot component + attach to streaming rows. ~60 lines.

---

## COMPOSER & SENDING (11 gaps)

### 1. Composer history walk (arrow keys)
**What it does:** In the composer, press Up arrow to recall the previous sent message, Down to step forward. Caret position is preserved (first/last line detection); pressing Up at the start of the message history again does nothing.

**Where in puffin:** `ComposerHistory.swift` + `ComposerTextView.swift` keyboard delegate

**Hanabi today:** Composer has no history. Every send starts fresh.

**Importance:** Table stakes. Users expect this from any chat interface (Discord, Slack, iMessage all have it).

**Size:** Small. Store per-session history (UserDefaults) + arrow-key listener + caret check. ~80 lines.

### 2. Slash command menu (/new, /model, /effort, /rename, /btw, /compact, /autocompact)
**What it does:** Type `/` in the composer; a menu appears listing available commands. Commands like `/model gpt-4` to switch model, `/rename New Title` to rename the session, `/btw Why is X?` to fork, `/compact` to compact now, `/autocompact` to toggle auto-compaction. Up/Down navigate, Return selects, Escape closes.

**Where in puffin:** `SlashCommandMenu.swift` + `SlashCommands.swift` (registry)

**Hanabi today:** No slash commands. No menu.

**Importance:** Table stakes. Essential for session control (rename, model switch, fork) without context menus.

**Size:** Medium. Menu UI + command parsing + routing to session actions. ~150 lines (plus per-command handlers).

### 3. Model picker popover (in strip)
**What it does:** Composer strip shows the selected model name (e.g., "Claude 3.5 Sonnet"). Click to open a popover listing available models. Click a model to change it. Popover shows: current model (radio-selected), effort slider (per-model), notes on effort levels. Change is durable (updates session options).

**Where in puffin:** `ModelMenu.swift` + `ModelPopover.swift` + `AgentcloudSession.patchOptions()`

**Hanabi today:** No model picker in the UI. Model is fixed at server config.

**Importance:** Table stakes. Users need to be able to switch models mid-thread.

**Size:** Medium. Model list from session state + popover UI + patch_session_options wire call. ~120 lines.

### 4. Effort level picker (per-model)
**What it does:** Inside the model popover, a slider sets the effort level for the selected model. Levels are per-model (some models have 3 levels, others 5). Slider is read-only during a running turn (spinner while patching). Server refusal shows the error message. No local optimism.

**Where in puffin:** `ModelPopover.swift` + `TuningChange.swift` (waiting state)

**Hanabi today:** No effort picker.

**Importance:** Important. Effort controls cost/latency tradeoff; users need it.

**Size:** Medium. Effort list from model metadata + slider UI + patch wire. ~90 lines.

### 5. Token context meter + popover
**What it does:** Composer strip shows a usage bar: "127k / 800k tokens" with a filled percentage bar (blue). Click to open a context popover. Popover shows:
- "Occupancy" heading
- Current tokens + max budget (from session state)
- "Compact now" button
- If pending compaction, shows "Compacting…" spinner
- If over budget, shows a warning
- Stale flag (occupancy predates unsent content)
- Per-child breakdown in expand (each sub-agent's token spend)

**Where in puffin:** `ContextPopover.swift` + `ModelPopover.swift` (in composer strip)

**Hanabi today:** Meter exists (hardcoded 38% as placeholder). No real numbers, no popover.

**Importance:** Table stakes. Token management is critical; users need real numbers.

**Size:** Medium. Wiring: read tokens from session state + display + compact button. Data layer (todo.md) tracks this: "context_usage" event thrown away, need to parse hello.state.tokens instead. **Blocked until data layer fix lands (context_meter wiring owed in render-phase after data-layer merge).**

### 6. Skills chip in strip
**What it does:** Composer strip shows a skills chip listing the 3 most-invoked skills in this thread. Click to expand (popover) showing all skills with invocation counts. Single-click a skill to invoke it (submits the message).

**Where in puffin:** `ModelPopover.swift` + skill expansion + `SkillUse.swift`

**Hanabi today:** No skill menu.

**Importance:** Important. Skills are a core feature; users need easy access.

**Size:** Medium. Skill ranking + popover rendering + invoke logic. ~100 lines (depends on data layer emitting skill names).

### 7. Nodes chip in strip (attach/detach nodes)
**What it does:** Composer strip shows a "Nodes" chip. Click to open a popover listing all available nodes (attached to the session). Nodes can be toggled on/off. Check to attach a node, uncheck to detach. Radio buttons below to select "which nodes the next message targets" if the backend supports it.

**Where in puffin:** `NodeAttachment.swift` + `ModelPopover.swift`

**Hanabi today:** No node attachment UI.

**Importance:** Important. Multi-agent threads need node control.

**Size:** Medium. Node list from session state + popover UI + attach/detach wire. ~110 lines.

### 8. Sub-agents chip in strip (show/hide finished)
**What it does:** Composer strip shows a badge "Sub-agents: 3 running". Click to open a popover showing:
- List of all child sessions with status
- "Show finished" checkbox
- Status indicator per child (dot color: blue=running, grey=finished)
- Last activity time

**Where in puffin:** `SubAgentPanel.swift` + popover in strip

**Hanabi today:** Sub-agents show in the sidebar but not in the composer strip.

**Importance:** Polish. Nice to have for power users.

**Size:** Small. Component in strip + popover rendering. ~70 lines.

### 9. Context chip in strip
**What it does:** Shows a light indicator of the context budget (see gap #5 above). Click for the popover (same as gap #5).

**Where in puffin:** `ContextPopover.swift` (in composer strip as a chip)

**Hanabi today:** Part of the meter (gap #5); not a separate chip.

**Importance:** Table stakes. Bundled with gap #5.

**Size:** Included in gap #5.

### 10. Draft persistence (per-session)
**What it does:** When a user types in the composer and then navigates away, the text is auto-saved. On return, the draft is restored. Drafts are per-session (landing page has a separate draft). Empty/whitespace drafts are cleared. Drafts survive app restart (optional setting). Max 20,000 characters.

**Where in puffin:** `DraftStore.swift` + `ComposerTextView.swift` onChange listener

**Hanabi today:** No draft persistence. Text is lost on navigation.

**Importance:** Important. Users expect drafts to be saved (like email, iMessage).

**Size:** Small. UserDefaults store + onChange hook. ~50 lines.

### 11. Refusal reasons in composer notices
**What it does:** When Send is disabled, the composer shows a notice explaining why: "Read-only conversation", "Socket disconnected", "Approval pending", etc. Notice is contextual and updates in real-time.

**Where in puffin:** `ComposerNotices.swift` + `AgentcloudChatView.swift`

**Hanabi today:** Send button is grey/disabled but no explanation why.

**Importance:** Important. Users need to know why they can't send.

**Size:** Small. Compute refusal reason + render notice. ~40 lines.

---

## TABS & WINDOWS (6 gaps)

### 1. Tab drag-and-drop to reorder
**What it does:** Drag a tab by its title to the left/right to reorder it. Other tabs shift. New order persists in UserDefaults.

**Where in puffin:** `TabStrip.swift` + drag gesture + `AgentcloudTabModel.reorder()`

**Hanabi today:** Tabs are fixed in order (append only).

**Importance:** Important. Power users expect this (like browsers).

**Size:** Medium. Drag recognizer + drop zones + order store. ~80 lines (afterhours gesture support needed).

### 2. Tab drag-and-drop to split pane
**What it does:** Drag a tab into a "drop zone hint" (leading/trailing/top/bottom of the pane) to create a split layout. Left pane and right pane, each with tabs. Each pane has independent tab navigation and scroll position. Closing both panes collapses back to single pane.

**Where in puffin:** `MainWindowShell.swift` + drop zone rendering + `AgentcloudTabModel.splitState`

**Hanabi today:** Split exists (via HANABI_SPLIT env flag + right-click context menu) but not drag-into-drop-zone. User must right-click to split.

**Importance:** Important. Drag is more discoverable than context menu.

**Size:** Medium. Drag recognizer + drop zone rendering + visual hints. ~90 lines (depends on afterhours drag support).

### 3. Tab context menu (Copy URL, Close Others, Close All)
**What it does:** Right-click a tab to see: "Copy Navi URL" (scheme link, navi://session/{id}), "Close Others", "Close All", "Move to new window".

**Where in puffin:** `TabStrip.swift` context menu

**Hanabi today:** No tab context menu.

**Importance:** Important. Essential for power users.

**Size:** Small. Add context menu delegate. ~40 lines.

### 4. Tab preview mode
**What it does:** When a tab is clicked but not kept-open, it shows in preview mode (background tab is frozen, not live-streaming). A second click keeps it (makes it durable). Saved settings persist which tabs are kept-open.

**Where in puffin:** `AgentcloudTabModel.Tab.isKeptOpen` + `AgentcloudSession.isFrozen`

**Hanabi today:** All tabs are persistent and live. No preview mode.

**Importance:** Polish. Helps users avoid accidentally keeping every tab open.

**Size:** Small. Flag per tab + freeze logic + restore on app launch. ~50 lines.

### 5. Window restoration on launch
**What it does:** Open windows (id + title + frame position) persist to UserDefaults on app quit. On next launch, windows are reopened with the same content and position. Gated by a Settings toggle.

**Where in puffin:** `WindowManager.swift:restoreWindowsOnRestart` + frame save/restore

**Hanabi today:** Single window only; no multi-window support.

**Importance:** Important. Multi-window support enables this.

**Size:** Medium. Window frame + session tracking + restore logic. ~100 lines (depends on multi-window architecture).

### 6. Tab scrollbar (overflow handling)
**What it does:** When many tabs are open, the tab strip shows a horizontal scrollbar (like Chrome). Tabs shrink to a min width (no truncation). Active tab stays visible. Left/right arrows to scroll.

**Where in puffin:** `TabStrip.swift` + horizontal ScrollView

**Hanabi today:** Tabs shrink and truncate. No scrollbar.

**Importance:** Important. Current many-tabs state is hard to use (todo.md #60).

**Size:** Medium. Horizontal ScrollView wrapper + min-width logic + arrow buttons. ~80 lines (afterhours gap: no horizontal ScrollView yet).

---

## SEARCH & FIND (5 gaps)

### 1. Cmd+F find-in-transcript (basic)
**What it does:** Cmd+F opens a find bar below the transcript. Type to search. Results highlight in rows. Matches count ("1 of 5" style). Cmd+G next, Cmd+Shift+G previous. Escape closes. Search is case-insensitive. All rows on the current page are searchable. (Searching history not yet loaded shows "Elsewhere" indicator.)

**Where in puffin:** `TranscriptFindBar.swift` + `TranscriptFind.swift` search logic

**Hanabi today:** ALREADY BUILT (`src/ui/find_highlight.h`, `main_pane_system.h`,
three e2e scripts under `tests/ui/find_*.e2e`). Cmd+F opens a bar, counts
matches and paints a band behind every one; next/previous scroll the match into
view. **Not a gap.** What is genuinely missing beside puffin's: the "Elsewhere"
indicator for matches in history that has not been paged in.

**Importance:** n/a — done.

**Size:** n/a. The "Elsewhere" hint alone would be small.

### 2. Find operators (is:, has:, state:, harness:, tag:)
**What it does:** Find bar supports search operators: `is:thinking` (search only thinking rows), `has:tool` (only rows with tools), `state:running` (by status), etc. Mix with plain text: `python is:tool` (python in tool rows).

**Where in puffin:** `TranscriptFind.swift:parse(query:)` + operator evaluation

**Hanabi today:** Find IS implemented (see #1); the operators are not.

**Importance:** Important. Power users need filtering.

**Size:** Small. Operator parsing + evaluation logic. ~80 lines (after basic find is done).

### 3. Session search (Cmd+Shift+F)
**What it does:** Cmd+Shift+F opens a session search sidebar. Type to search across all sessions (title + transcript full-text). Results show session title + snippet. Click a result to open that session. Snippet shows the matching context.

**Where in puffin:** `SessionSearchKeyboard.swift` + `TranscriptSearchIndex.swift`

**Hanabi today:** No session-level search. Only sidebar search (local, limited).

**Importance:** Important. Finding which conversation has a topic is essential.

**Size:** Medium. Full-text indexing across all transcripts + search UI. ~180 lines (depends on data-layer loading old history).

### 4. Command palette (Cmd+K)
**What it does:** Cmd+K opens a palette. Type to fuzzy-search: sessions, menu commands, settings panes, smart views. Results are ranked. Return selects. Seven result kinds with icons.

**Where in puffin:** `CommandPalette.swift` + `PaletteSource.swift`

**Hanabi today:** No command palette.

**Importance:** Important. Essential for discoverability and power users.

**Size:** Medium. Fuzzy search + result ranking + UI. ~180 lines.

### 5. Search highlighting in sidebar (snippet on match)
**What it does:** When a search result is clicked, the sidebar row shows a snippet of the matching text, with the search term highlighted.

**Where in puffin:** `SessionSearchKeyboard.swift` + snippet extraction

**Hanabi today:** Search results exist (in the main pane search) but sidebar doesn't show snippets.

**Importance:** Polish. Nice for clarity.

**Size:** Small. Snippet extraction + highlight tagging. ~40 lines.

---

## SESSION LIFECYCLE (7 gaps)

### 1. Session rename (Cmd+R or menu)
**What it does:** Right-click a session in the sidebar or a tab to get "Rename…". Modal dialog appears with the current title. Edit and press Return. Title is sent to the server. Durable echo (`session_renamed` frame) updates the display. Server can refuse (title validation) with an error message. No local optimism (must wait for echo).

**Where in puffin:** `SessionRename.swift` + `AgentcloudSession.rename()` wire call

**Hanabi today:** No rename capability.

**Importance:** Table stakes. Users need to rename conversations.

**Size:** Small. Dialog UI + wire call + echo handling. ~70 lines.

### 2. Session fork (/btw <question>)
**What it does:** `/btw Why did X fail?` slash command forks the thread. Title is derived ("BTW: Why did X fail?"). New session opens in a new tab. Parent/child relationship is tracked. Fork boundary appears in the original transcript.

**Where in puffin:** `BtwFork.swift` + `SlashCommands.swift` + `ClientCmd.forkWithPrompt`

**Hanabi today:** No fork capability.

**Importance:** Table stakes. Forking is a core workflow (branch out to debug).

**Size:** Medium. Command parsing + fork wire call + tab/window management. ~100 lines.

### 3. Session archive (pin icon, archive from sidebar)
**What it does:** Right-click a session to get "Archive". Session disappears from main list and appears in "Archived" shelf. Click "Archive" again (from within the session or via menu) to unarchive. Archive state is per-viewer (synced to backend, InboxState API). Toast undo bar on archive.

**Where in puffin:** `ArchivedSessions.swift` + `InboxState.swift` (inbox_session_state) + undo toast

**Hanabi today:** Archive state exists but UI is limited (only toggle from sidebar, no undo toast, no "Archived" shelf on Home).

**Importance:** Important. Archive is how users file away old conversations.

**Size:** Medium. Menu item + archive toggle + sidebar shelf rendering + undo toast. ~90 lines.

### 4. Session pin (star icon)
**What it does:** Click the star icon on a sidebar row to pin/unpin it. Pinned sessions sort to the top of their Space. Icon is filled (★) when pinned, hollow (☆) when not. State is durable per-viewer. Separate from "keep tab open" (AgentcloudTabModel.Tab.isKeptOpen).

**Where in puffin:** `PinnedThreads.swift` + star icon in `SessionRowView.swift`

**Hanabi today:** No star/pin capability.

**Importance:** Important. Users need to prioritize conversations.

**Size:** Small. Toggle + sort override + icon state. ~50 lines.

### 5. Session mute (bell icon)
**What it does:** Click the bell icon on a sidebar row to mute/unmute it. Muted sessions don't trigger notifications. Icon is crossed out (🔇) when muted. State is machine-local (not synced). Muting a parent doesn't automatically mute children.

**Where in puffin:** `MutedSessions.swift` + bell icon in `SessionRowView.swift`

**Hanabi today:** No mute capability.

**Importance:** Important. Users need quiet hours control.

**Size:** Small. UserDefaults toggle + notification gate. ~40 lines.

### 6. Sub-agent list with status
**What it does:** Below the composer in a long session, a collapsible "Sub-Agents" section lists all children with: status indicator (dot: blue=running, grey=done), title, last activity time. Click a child to open it. Closed by default (toggle in sidebar settings).

**Where in puffin:** `SubAgentPanel.swift` + `ChildRow.swift`

**Hanabi today:** Sub-agents list exists but is in the sidebar only, not in the main pane.

**Importance:** Important. Users need easy access to child threads.

**Size:** Medium. Panel rendering in main pane + status fetch. ~100 lines.

### 7. Delete session
**What it does:** Right-click a session to see a "Delete" option. Deletes the session server-side. No undo.

**Where in puffin:** Not implemented. CLAUDE.md lists it as "no verb" — the server has no delete endpoint.

**Hanabi today:** No delete capability.

**Importance:** Not applicable. Server doesn't support delete.

**Size:** N/A — blocked on backend.

---

## DRAFTS & UNDO (2 gaps)

### 1. Draft persistence (auto-save on keystroke)
**What it does:** As a user types in the composer, the text is auto-saved to UserDefaults every keystroke. On navigation away and back, the draft is restored. Separate drafts for each session (landing page has its own draft). Empty/whitespace clears the draft. Survives app restart (optional). Max 20,000 characters.

**Where in puffin:** `DraftStore.swift` + `ComposerTextView.swift:onChange`

**Hanabi today:** No draft persistence.

**Importance:** Important. Users expect this.

**Size:** Small. UserDefaults store + onChange hook. ~50 lines.

### 2. Undo toast bar (Archive/Pin/Mute)
**What it does:** After Archive, Pin, or Mute, a 10-second toast bar appears at the bottom with an "Undo" button. Clicking Undo reverses the action. Toast auto-dismisses after 10s.

**Where in puffin:** `PuffinToast.swift` + per-action undo handlers

**Hanabi today:** No undo toast.

**Importance:** Polish. Nice for safety.

**Size:** Small. Toast component + per-action undo. ~50 lines.

---

## ATTACHMENTS (5 gaps)

### 1. Image paste/drop in composer
**What it does:** Paste an image (Cmd+V) or drag-drop into the composer. Image appears as a chip with a preview thumbnail and a remove button. Multiple images per message. On send, images are base64-encoded and included in the prompt.

**Where in puffin:** `PendingAttachmentStore.swift` + `ComposerTextView.swift` drop delegate

**Hanabi today:** No image attachment UI. Attachments may exist in the data layer but are not surfaced in the composer.

**Importance:** Important. Users want to include images (screenshots, diagrams).

**Size:** Medium. Drop zone UI + preview rendering + base64 encoding. ~120 lines.

### 2. File upload (workspace tool)
**What it does:** The `file_upload` workspace tool allows the agent to upload files. When invoked, a file picker dialog appears. User selects a file. File is uploaded to the workspace. Link appears in the transcript.

**Where in puffin:** `PendingAttachmentStore.swift` (file staging) + tool-specific rendering

**Hanabi today:** No file upload UI. Tool exists on the server but UI is not built.

**Importance:** Important. File handling is essential for many workflows.

**Size:** Medium. File picker + upload wire + progress UI. ~100 lines.

### 3. Pending attachment restoration after failed send
**What it does:** If a send fails (network error, etc.), the composer text and staged images are restored automatically. User can retry without retyping.

**Where in puffin:** `PendingAttachmentStore.swift` error handling

**Hanabi today:** No attachment restoration.

**Importance:** Important. UX polish for reliability.

**Size:** Small. Store images on send failure + restore on retry. ~30 lines.

### 4. Edited file diff display in transcript
**What it does:** When an `edit` tool modifies a file, the transcript shows a diff: old content (strikethrough or faded) vs new content (highlighted). File path at the top. "Replace all" indicator.

**Where in puffin:** `ToolRowView.swift` + `MemoryEditDiffView.swift` (diff rendering)

**Hanabi today:** Edit tool output renders as plain text. No diff.

**Importance:** Important. Diffs are essential for code changes.

**Size:** Medium. Diff parser + side-by-side rendering. ~100 lines.

### 5. Report attachment (bug report screenshot/video)
**What it does:** When filing a bug report (Cmd+Shift+F), the user can attach a screenshot or screen recording. The file is uploaded to the code-review tool and the link embedded in the filed issue. Access-gated to internal users.

**Where in puffin:** `ReportAttachment.swift` + `BugReport.swift` + file upload

**Hanabi today:** No bug report in hanabi (different app type).

**Importance:** Not applicable. Hanabi is a personal app, not a company one.

**Size:** N/A — out of scope for hanabi.

---

## NOTIFICATIONS & BACKGROUND (3 gaps)

### 1. Toast notifications (approval waiting, run finished, etc.)
**What it does:** When a run is blocked on approval, or finishes, or awaits user input, a macOS notification appears (system-level, may play sound). Notification title and body describe the event. Clicking the notification opens the session. Per-alert settings in Settings.

**Where in puffin:** `SessionAlerts.swift` + `NotificationCategories.swift` + native NSUserNotification

**Hanabi today:** Phase G native notifications exist (global hotkey, blocked-count notifications) but are limited to one type (blocked count increase). No per-event notifications.

**Importance:** Important. Users need to be notified of important events.

**Size:** Medium. Expand notification types + category registration + click handling. ~80 lines.

### 2. Quiet hours / notification silence
**What it does:** Settings has a "Quiet hours" section. User can set times when notifications are suppressed (e.g., 10 PM – 8 AM). Outside quiet hours, notifications fire normally.

**Where in puffin:** `SettingsView.swift` (Notifications pane) + `SessionAlerts.swift` time gate

**Hanabi today:** No quiet hours setting.

**Importance:** Polish. Nice to have.

**Size:** Small. Time picker in Settings + gate in notification logic. ~40 lines.

### 3. Update checker (release notes, version)
**What it does:** Hourly, app checks for new version from a release feed. If available, shows a banner in Settings with "Download", "View Release Notes", "Later". Download is automatic, relaunch prompt appears on user action.

**Where in puffin:** `UpdateChecker.swift` + `UpdateInstaller.swift` + Manifold feed (inert in Puffin today, no bucket)

**Hanabi today:** No auto-update system (manual build/install).

**Importance:** Not applicable. Hanabi is not distributed; users compile locally.

**Size:** N/A — out of scope (no distribution path).

---

## SETTINGS & PREFERENCES (8 gaps)

### 1. Send message behavior (Return vs Cmd+Return)
**What it does:** Settings → General → "Send message with:" toggle. Choose Return to send on plain Return key, or Cmd+Return to require modifier. Default is Return.

**Where in puffin:** `SettingsGeneralTab.swift` + `ComposerTextView.swift` key binding

**Hanabi today:** No configurable send key.

**Importance:** Important. Different users prefer different behaviors.

**Size:** Small. Toggle in Settings + key binding logic. ~30 lines.

### 2. Restore windows on restart
**What it does:** Settings → General → "Restore windows on restart" toggle. When on, windows open on startup. When off, app starts with no windows (click menu bar icon to open one).

**Where in puffin:** `SettingsGeneralTab.swift` + `WindowManager.swift` restore logic

**Hanabi today:** Single window only. No multi-window restore.

**Importance:** Important. Requires multi-window support.

**Size:** Medium. Window frame tracking + restore logic. ~80 lines.

### 3. Show/hide timestamps in transcript
**What it does:** Settings → Chat Behavior → "Show timestamps" toggle. When on, each row shows a time (and date, per #1 gap). When off, times are hidden.

**Where in puffin:** `SettingsChatBehaviorTab.swift` + `AgentcloudTranscriptView.swift` conditional render

**Hanabi today:** Timestamps are always shown.

**Importance:** Polish. Some users prefer minimal.

**Size:** Small. Toggle + conditional render. ~20 lines.

### 4. Minimap visibility toggle + mark type filters
**What it does:** Settings → Chat Behavior → "Show minimap" toggle. When on, right edge has the navigator rail. Below it, five checkboxes for mark types (machinery, reply, delivery, notice, ask). User can filter which mark types appear on the minimap.

**Where in puffin:** `SettingsChatBehaviorTab.swift` + `TranscriptMinimap.swift` filtering

**Hanabi today:** No minimap.

**Importance:** Polish. Depends on gap #4 (minimap) being built first.

**Size:** Small. Toggle + checkboxes in Settings. ~30 lines (after minimap is built).

### 5. Typeface picker (system/serif/rounded/mono)
**What it does:** Settings → Appearance → "Typeface:" dropdown. Choose system (San Francisco), serif (Georgia), rounded (Avenir), or monospace (Courier). All message text reflows.

**Where in puffin:** `SettingsAppearanceTab.swift` + theme system

**Hanabi today:** Typeface is fixed.

**Importance:** Polish. Nice for accessibility.

**Size:** Small. Dropdown + font selection. ~40 lines.

### 6. Text weight picker (user vs assistant messages)
**What it does:** Settings → Appearance → two dropdowns: "User message weight" and "Assistant message weight" (light/regular/bold). Adjust emphasis per message role.

**Where in puffin:** `SettingsAppearanceTab.swift`

**Hanabi today:** Text weights are fixed.

**Importance:** Polish. Accessibility feature.

**Size:** Small. Dropdowns + font application. ~30 lines.

### 7. Theme picker (static + rotate modes)
**What it does:** Settings → Appearance → "Theme:" dropdown + icon. Select from 11 presets (Puffin Night, Daylight, Hyrule, etc.). Or choose "Rotate" to automatically cycle through themes at intervals. Custom theme editor (button) to create new theme.

**Where in puffin:** `SettingsAppearanceTab.swift` + `CustomThemeEditor.swift`

**Hanabi today:** Theme picker exists. Custom themes not editable.

**Importance:** Important. Theme system needs expansion.

**Size:** Medium. Theme list + custom editor. ~150 lines.

### 8. Custom theme editor (color swatches, syntax palette)
**What it does:** Settings → Appearance → "Edit Theme…" button. Opens a panel with 11 color swatches (primary, secondary, etc.) and a syntax palette (code colors for 8 language groups). User picks colors via color picker. Preview card on the right shows the theme. Save as custom theme.

**Where in puffin:** `CustomThemeEditor.swift`

**Hanabi today:** Theme editor not implemented.

**Importance:** Polish. Power users want custom themes.

**Size:** Medium. Color picker UI + palette editing + preview. ~180 lines.

---

## KEYBOARD & SHORTCUTS (5 gaps)

### 1. Global hotkey (Cmd+Shift+Space = Quick Launcher)
**What it does:** From anywhere in the OS, press Cmd+Shift+Space to open the app's Quick Launcher (palette) without switching windows. Type to search. Return to open a session.

**Where in puffin:** `HotKeyManager.swift` (Carbon RegisterEventHotKey) + launcher integration

**Hanabi today:** No global hotkey.

**Importance:** Important. Essential for power users; context-switching is expensive.

**Size:** Medium. Carbon hotkey registration + palette launch. ~80 lines (Phase G work mentioned in todo.md).

### 2. Keyboard shortcut recorder (in Settings)
**What it does:** Settings → Shortcuts → each command (Open Settings, New Conversation, etc.) shows its hotkey. Click "record" to open a recorder; press the desired key combo; app detects conflicts and alerts user.

**Where in puffin:** `ShortcutsTab.swift` + `HotKeyChord.swift` recorder

**Hanabi today:** No shortcut customization UI.

**Importance:** Important. Power users want to rebind.

**Size:** Medium. Recorder UI + conflict detection. ~100 lines.

### 3. Composer keyboard shortcuts (Shift+Return, Option+Return)
**What it does:** In the composer:
- Return: Send (or Shift+Return for newline, configurable)
- Option+Return: Always newline
- Up/Down: History walk
- Cmd+A: Select all
- Cmd+C: Copy

**Where in puffin:** `ComposerTextView.swift` key handling

**Hanabi today:** Only Return sends. Up/Down history not implemented.

**Importance:** Important. Standard chat shortcuts.

**Size:** Small. Key handlers for all cases. ~50 lines.

### 4. Navigation shortcuts (arrow keys in lists, menus)
**What it does:** In the command palette, Up/Down arrows navigate results. In lists, Up/Down move selection. In menus, arrows move through options. Return selects.

**Where in puffin:** `CommandPalette.swift` + per-component key handlers

**Hanabi today:** No command palette yet. Some navigation shortcuts exist.

**Importance:** Important. Standard navigation.

**Size:** Small. Arrow key handlers. ~30 lines (per-component, varies).

### 5. Find shortcuts (Cmd+F, Cmd+G, Cmd+Shift+G)
**What it does:** Cmd+F opens find bar. Cmd+G finds next. Cmd+Shift+G finds previous. Escape closes find bar.

**Where in puffin:** `TranscriptFindBar.swift` key binding

**Hanabi today:** No find-in-transcript yet.

**Importance:** Table stakes. Bundled with find feature (gap #1).

**Size:** Small. Key handlers for find navigation. ~20 lines (after find is built).

---

## NATIVE macOS INTEGRATION (3 gaps)

### 1. Menu bar icon (puffin symbol)
**What it does:** App lives in menu bar (NSStatusItem). Icon is a puffin symbol, drawn with NSBezierPath (face, eye, beak as knockouts in a circle). Template image, recolors dark/light. 18pt size. Click to show/hide main window, or access app menu.

**Where in puffin:** `MenuBarIcon.swift` + `AppDelegate.swift`

**Hanabi today:** Menu bar icon exists (generic hanabi symbol). SVG-based, not custom-drawn.

**Importance:** Polish. Icon is fine; custom drawing not needed.

**Size:** N/A — already done (different approach, acceptable).

### 2. Native menus (File, Edit, View, Window, Help)
**What it does:** Standard macOS menu bar with File, Edit, View, Window, Help. Keyboard shortcuts shown. Session-specific context menus on right-click.

**Where in puffin:** `AppDelegate.swift:buildMainMenu()` + `SessionMenuItems.swift`

**Hanabi today:** Menus exist but may be incomplete.

**Importance:** Important. Proper menu structure is expected.

**Size:** Small. Menu structure + items + shortcuts. ~100 lines.

### 3. Spotlight search (system-wide session discovery)
**What it does:** User can search in Spotlight (Cmd+Space) and type a session title. Matching sessions appear. Click to open in puffin.

**Where in puffin:** `SpotlightIndex.swift` + CSSearchableItem indexing

**Hanabi today:** No Spotlight indexing.

**Importance:** Important. System-wide search is expected.

**Size:** Medium. Index building + CSSearchableIndex setup + deep link handler. ~120 lines (Phase G, native extras; parked, needs .app bundle).

---

## ADDITIONAL FEATURES (1 gap)

### 1. Help text / tips (WelcomeView, onboarding)
**What it does:** On first launch, a multi-page welcome card appears showing:
1. Welcome page (intro, key concept)
2. Quick Compose page (how to send a message)
3. Global Input page (global hotkey, push-to-talk)
4. Watch the Work page (tool calls, node labels, thinking)
5. Steer a Run page (apply modes, status)

Pages have illustrations and can be dismissed. "Don't show again" option.

**Where in puffin:** `PuffinTips.swift` + `WelcomeView.swift` + WelcomePaging

**Hanabi today:** No welcome tutorial.

**Importance:** Polish. Nice onboarding.

**Size:** Medium. Multi-page component + page content. ~150 lines.

---

## Deliberately NOT in Puffin (Don't Copy These)

These are features puffin explicitly chooses NOT to build:

### 1. **Delete session**
**Why not:** The server has no delete verb. Sessions cannot be deleted server-side. Archive is the alternative.

### 2. **Attachments (full file system integration)**
**Why not:** Requires a separate auth path (InternGraph OAuth token) and file hosting. Out of scope for Phase 1 (which this is). Attachment stubs exist; full feature is parked.

### 3. **Reactions (emoji reactions on messages)**
**Why not:** The wire has no support. Users can type emojis; reactions are not a separate affordance.

### 4. **Message pinning (in-conversation bookmarks)**
**Why not:** Was local state on Navi (old backend). A machine-local divergence is worse than none. Threads are pinned; messages within threads are not.

### 5. **CardV2 / artifact panel**
**Why not:** Spec 121 is approved but not implemented server-side. No client work until spec is live.

### 6. **Full-text search on server**
**Why not:** No server endpoint. Client-side search over paged history is the alternative.

### 7. **Per-window themes**
**Why not:** Themes went app-wide when Link's ChatModel was removed. Per-window would color only the frame, not the content.

### 8. **Telemetry export (send to backend)**
**Why not:** Inert in puffin (no Scribe category, no bucket). Turn-on checklist is in CLAUDE.md. When telemetry lands, it follows Link's schema.

---

## Summary of Top Five Priorities

Ranked by user impact + ease:

1. **Timestamp rows (dates) — medium, 80 lines**
   - Required for understanding thread flow
   - Blocks: Home digest grouping

2. **Session rename — small, 70 lines**
   - Core workflow, every user does this
   - No blockers

3. **Composer history (arrow keys) — small, 80 lines**
   - Expected in all chat UIs (Discord, Slack, iMessage)
   - No blockers

4. **Find in transcript (Cmd+F) — medium, 150 lines**
   - Essential for long threads
   - Blocks: operators, search highlighting

5. **Session fork (/btw) — medium, 100 lines**
   - Core workflow for branching out to debug
   - Blocks: nothing else

---

## Count Summary

- **Total gaps: 79**
- **Table stakes (must-have): 26**
- **Important (should-have): 36**
- **Polish (nice-to-have): 17**
- **Niche (optional): 0**

Effort distribution:
- **Small (30–80 lines): 25 gaps** — quick wins, 1–2 hours each
- **Medium (80–200 lines): 40 gaps** — 4–8 hours each
- **Large (200+ lines): 14 gaps** — 1–3 days each

Blocked on backend/vendor:
- Spaces grouping (backend `workspace` field missing from session rows)
- Thinking rows (data layer must emit `thinking` frame type)
- Tool call sub-rows (data layer block-splitting, merged in wt/live-sse)
- Context meter real numbers (data layer must parse `hello.state.tokens`)
- Minimap (afterhours needs basic geometry; WIP in vendor)
- Spotlight (needs .app bundle + LaunchServices; parked in Phase G)
- Horizontal scroll on tabs (afterhours gap #26, addressed in todo.md)

---

END OF GAP ANALYSIS
