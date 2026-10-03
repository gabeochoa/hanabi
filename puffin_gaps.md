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

**Total gaps: 84** across 13 functional areas.

| Area | Count | Priority |
|------|-------|----------|
| Sidebar & Navigation | 9 | 4 table-stakes + 5 polish |
| Transcript & Rendering | 17 | 6 table-stakes + 10 polish + 1 niche |
| Composer & Sending | 11 | 3 table-stakes + 8 polish |
| Tabs & Windows | 8 | 5 important + 3 polish |
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

**Hanabi 2026-10-02 (lead lane): BUILT.** The Spaces list (`/api/graphql` `metamate_projects`, D10) and the session->Space index (`xfb_agentcloud_session_list_for_viewer` with no filter, `session_id space { id }`, paged by cursor; `api/spaces_wire.h`) give every filed thread a `space_group`; the sidebar sections by Space first, in the Space list's order, before workspace folders, and unfiled threads stay in Recents. Headers show the Space's name, not its emoji (no loaded face draws one; afterhours_gaps.md #48). Re-walked every ten minutes. `tests/ui/sidebar_sections_threads_by_space.e2e`, test_spaces_wire. Mock-verified.

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

**Hanabi 2026-10-02:** stale -- named sections (workspace folders, and now Space sections) collapse on their header and arrive collapsed; `sidebar_sections_threads_by_space.e2e` opens one.

**Importance:** Polish. Nice for power users with many sessions.

**Size:** Small. Add collapse state per Space + toggle on header click. ~40 lines.

---

## TRANSCRIPT & RENDERING (17 gaps)

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

### 15. Transcript rows the export/copy mapper cannot spell (data-model delta, not a feature)
**What it does:** The reference's per-row Markdown mapper (`TranscriptMarkdown.body(of:)`, used by both Copy Turn and Export to Clipboard) has arms for artifact rows (`*(artifact: <title> — version <v>)*`), elicitation rows (`### **The agent asks**` + prompt/options/answer lines), context rows (`*(<label>: <detail>)*`), fork boundaries (`---\n\n*(forked from …)*`), and audio-attachment lines on user/delivery rows.

**Where in puffin:** `TranscriptExport.swift` `body(of:)` (:74-160)

**Hanabi today:** artifact rows are modeled and mapped (`EventKind::Artifact`, 2026-09-17: `*(artifact: <name> — <type · size>)*`). `api::Message` still models none of elicitation / context / fork-boundary / audio-attachment lines, so `hanabi::transcript_copy::body_of` has no arm for them and such rows fall through the generic act/text arms. This is a pre-existing Copy Turn delta that Export to Clipboard (2026-09-14) inherits by design (it reuses `body_of`); neither feature is "at parity" for these kinds and neither claims to be. Closing it is a model change first (new `api::Message` kinds parsed from the wire), then one mapper arm each.

**Importance:** Polish. Threads that carry these rows export with a generic line where the reference writes a specific one; nothing is dropped silently except the row's specific wording.

**Size:** Medium. The hard part is the wire parsing for five row kinds, not the mapper.

### 16. Export header title falls back to the id, not a status subject/headline (data-model delta)
**What it does:** The reference's export header uses the catalog row's `displayTitle`: a human-set title wins even when it equals the create placeholder; otherwise the stored title unless placeholder; otherwise the session's status subject or headline; otherwise eight characters of the id (`AgentcloudSessionList.swift:230-240, :433, :449`).

**Where in puffin:** `AgentcloudSessionList.swift` (`copyableTitle`, `ThreadTitle.named`)

**Hanabi today:** `transcript_copy::export_title` (2026-09-14) matches the stored-title-unless-empty-or-placeholder rule and the eight-character id fallback, trimming whitespace only. `api::SessionSummary` carries no `title_is_human` and no status subject/headline, so the "human title beats placeholder" and "subject/headline before id" branches have no inputs and are not emulated.

**Importance:** Niche. Only an unnamed thread with a status subject reads differently (id characters instead of the subject).

**Size:** Small once the fields exist on the wire model; the mapper is two lines.

### 17. Artifact rows: version switcher, follow-latest selection, truncated large reads, cross-page hide ordering
**What it does:** The reference's artifact tray keeps one record per artifact: a version switcher backed by `GET /artifacts/{id}/versions` (cursor-paged, `is_latest`/`is_first`/`content_state`), a `selection` of `explicit_version` (pinned) versus follow-latest so a row moves to a new version unless pinned, reads capped at 32 MB and drawn as a truncated prefix with a banner and an Open-in-Web exit, an `artifact_hidden` gate keyed on the last seq that moved the artifact's visibility so a backward page's older show cannot resurrect a hidden row, and a Retry toast when an archive/write did not land.

**Where in puffin:** `SessionArtifacts.swift`, `ArtifactVersions.swift`, `ArtifactContent.swift` (0.7.4 review cut)

**Hanabi today:** An artifact row draws the version it was shown with; no versions read, no switcher, no follow-latest (a new version is a new row when re-shown). Reads above 32 MB are REFUSED with the reason "larger than 32 MB; open in the web app" (no truncated prefix, no button: hanabi has no artifact web URL and invents none). `artifact_hidden` is honored with a seq gate (a hide older than a show is ignored; the row stays, marked hidden) for every row the parse or the refetch window re-delivers; only a hide for a row older than the refetch window is unreached. Metadata (name, size) before the fetch comes only from create/version events in the loaded page, as in the reference; the fetch itself needs neither (type from the response, then the byte signature). Audio artifacts play from their row since 0c640e2 (Knots kt-nmbp; the reference's media renderer plays them through AVPlayer -- `ArtifactRenderers.swift` -- so the older note that it plays none is stale): Play / Pause, a bar, the time; formats the player cannot read say so. Reads above 32 MB are REFUSED with the reason "larger than 32 MB; open in the web app" (no truncated prefix, no button: hanabi has no artifact web URL and invents none). `artifact_hidden` is honored within a parsed page with a seq gate (a hide older than a show is ignored; the row stays, marked hidden); a hide whose show is in another page is not applied. Metadata (name, size) before the fetch comes only from create/version events in the loaded page, as in the reference; the fetch itself needs neither (type from the response, then the byte signature).

**Importance:** Polish. Multi-version artifacts and very large files are the affected cases.

**Size:** Medium for the versions read and switcher; small for follow-latest; the truncated-read banner needs a text artifact renderer first.

### Wire tags without a client decision (2026-09-19)

Two tags the platform shipped after the reference's contract (`clients/rust/generated` at 11ff6299):

- `confinement_changed` — a mid-session change to the session's own confinement ask, REPLACING like `options_changed`; the platform declares it additive and inert for an old reader. The delivered reference has no handling of it and hanabi holds no confinement state (`fold_options_changed` reads model/effort only), so it is SILENT here. The repl draws "confinement: filesystem X, network Y"; a notice in this client is a future decision, not parity.
- `voice_turn` — one recorded voice turn (speaker + text + a recorder status). The delivered reference parks it as undecided-with-live-verb (WireTypes.swift :310-318, :847-853). Hanabi classifies it explicitly UNSUPPORTED (`is_unsupported_wire_event`): the row draws as the Unsupported marker with the tag and none of the payload — no speaker, no text, no call id. Rendering voice turns as dialog is a parity gap, not a decision this client has made.

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

## TABS & WINDOWS (8 gaps)

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

**Hanabi 2026-10-02 (lead lane): BUILT.** A conversation tab dragged down out of the strip and dropped on the right half of the pane area opens in the split (the tab menu's Open in Split by hand); while held there the drop zone is tinted; a surface tab never splits; vertical motion only counts once the pointer has left the strip, so a wobbly click still clicks (`tab_bar_system.h` split_drop_target; `dragging_a_tab_into_the_pane_splits_it.e2e`).

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

### 7. Every open tab holds a live session (attachment architecture)
**What it does:** In the reference, every open conversation tab keeps its session object live — `WindowManager.liveSession(id:)` answers for any open tab, foreground or background — so actions gated on "attached" (Export to Clipboard, `isCopyable`) are enabled for every open tab, and a background tab's export reads its live fold.

**Where in puffin:** `WindowManager.liveSession(id:)`; `TranscriptExport.swift:306-308` (`isCopyable`)

**Hanabi today:** A live session exists only for a thread a PANE shows (`pane.openSession`, read by `session_with_messages`); a thread open in a kept BACKGROUND tab keeps its transcript in the `transcriptCache` but holds no live session. Export to Clipboard (2026-09-14) therefore offers the row disabled for a background tab — by design for this increment: the contract forbids a cache read or a fetch on the copy path — and the script `export_to_clipboard_copies_the_attached_thread_as_loaded` pins that boundary (kept background tab with cached history → dimmed, writes nothing). Closing the gap is an attachment-architecture change (live sessions per open tab, or a pane-independent attach), not a menu change.

**Importance:** Important. A reader with several tabs open expects Export (and any future attached-only action) on all of them, not only the two panes.

**Size:** Large. The hard part is what "live" means for a tab no pane draws — stream subscription, refetch cadence and memory for every open tab.

### 8. Archive and Close Tab: refusal flash, Retry on an unlanded archive, undo scope, pick after the tab closed
*(Shared block as of 2026-09-19, both menus: Pin/Unpin → Mute/Unmute → Snooze ▸ (the model's presets as a real submenu on both arms; while snoozed a single row "Snoozed until <due>" that unsnoozes) → divider → Archive/Unarchive → [Archive and Close Tab, tab only] → divider → Open in split. The delivered reference reads snooze in exactly three places — the two menus and the dock badge's own count; its sidebar lists, counts and sections carry no snooze term, so nothing moves or hides here either. Open deltas: the Custom… leaf runs the native phrase prompt (a real submenu divider + last leaf) only where a visible window can host it — headless and any drawn-only arm end at the presets, a stated gap until a drawn prompt exists; the windowed script proves the plumbing with presentation bypassed (an e2e-only dispatch hold keeps the request queued, the typed result is injected) because a dispatched runModal parks the frame loop; the NSAlert itself is proven only by the owned-window gesture; the dock badge does not exist in this client yet, so its snooze exclusion lands with it. Wake: both menus here apply DUE-TIME EXPIRY ONLY (snooze_menu.h active_until: a snoozedUntil at or before the pinned inbox clock reads Snooze again). The reference's rule is `SessionSnooze.dueAt(id, lastMessageUnixMs?, lastRunCompleteUnixMs?, now)` (2be91ed8 SnoozeMenu.swift, over SnoozeWake.swift's verdict): `until` while the verdict is active, nil once the instant arrived OR a message / a run settling landed after snoozedAt -- and an absent clock suppresses only the message/settle wake: due expiry still releases (SnoozeWake.swift `dueAtMs = nowSec >= snoozedUntil ? … : nil` runs regardless of clocks; the tab menu, which passes none, still reads Snooze again once due). Its TAB menu (TabStrip.swift :976 at 2be91ed8) passes no clocks, so due-only there IS parity; its SIDEBAR ROW menu (SessionMenu.swift) passes the catalog row's clocks -- `last_activity_unix_ms ?? last_event_unix_ms` and `last_run_complete_unix_ms` (AgentcloudSessionList.swift :594-599 at 2be91ed8, `NSNumber.uint64Value` -- a negative wraps rather than rejects, and SessionMenu.swift :162-163's `Int($0)` then traps; hanabi's catalog parser reads none of the three today) -- so the early wake is a ROW-MENU gap here: snooze_wake.h models the verdict predicate-for-predicate (same `now >= until` boundary, same `(snoozedAt+1)*1000` provably-after rule, same earliest-wins order message→settled; unit-pinned in test_snooze.cpp :289-292) but no view feeds it -- SessionSummary carries neither clock and summary_from_row reads neither -- so a row that gets a message while snoozed keeps reading "Snoozed until <due>" until that time. acknowledge-on-open has no view caller in the reference and none here. Open, not implemented by the 2026-09-19 correction. State is server-authoritative per client generation, in memory only; on an orchestrator with no matched web origin the row is absent. "Show linked-chat banner" remains open.)*
**What it does:** The reference's tab menu carries Archive/Unarchive and, beneath, "Archive and Close Tab" (absent once archived; disabled while the tab is kept). Decided at click time from the live model: tab gone → archive only; kept → refused with the tab's pin flashing; else close first, then archive in the background. If the archive does not land it toasts "held on this Mac" with Retry. Single-thread archives have no undo toast (only multi-target ones do).

**Where in puffin:** `SessionMenuItems.swift`, `TabStrip.swift`, `ArchivedSessions.swift` (0.7.4 review cut)

**Hanabi today (2026-09-17):** both rows in the tab menu through the one archive writer (the sidebar drain); the combined act closes first, then archives with a directed, idempotent request and shows nothing (no toast, no undo -- as the reference when it lands); plain Archive/Unarchive keeps hanabi's existing toast + undo (the reference toasts only for multi-target archives -- pre-existing wording delta). Real constraint: hanabi archives LOCALLY (`archive_override` + Settings); there is no archive write on `api::Client`, so nothing can fail to land and there is no Retry / "held on this Mac" notice -- closing it needs an archive write API plus a landed/unlanded receipt. Affordance delta: a kept tab's refusal is silent (hanabi has no pin flash anywhere); nothing is archived or closed, as the reference. Menu lifetime: the drawn menu does not survive its tab (closed under it → no pick, nothing archived); the native menu's pick after the tab went archives, archive-only. Plain Close Tab still closes a kept tab (pre-existing). Group order: hanabi's archive group sits before Open in split; the reference's is last.

**Importance:** Polish. The refusal flash and Retry are the visible ones.

**Size:** Small for the flash; medium for unlanded-write tracking (needs the archive sync path to report).

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

- **Total gaps: 84**
- **Table stakes (must-have): 26**
- **Important (should-have): 37**
- **Polish (nice-to-have): 20**
- **Niche (optional): 1**

Effort distribution:
- **Small (30–80 lines): 27 gaps** — quick wins, 1–2 hours each
- **Medium (80–200 lines): 42 gaps** — 4–8 hours each
- **Large (200+ lines): 15 gaps** — 1–3 days each

Blocked on backend/vendor:
- Spaces grouping (backend `workspace` field missing from session rows)
- Thinking rows (data layer must emit `thinking` frame type)
- Tool call sub-rows (data layer block-splitting, merged in wt/live-sse)
- Context meter real numbers (data layer must parse `hello.state.tokens`)
- Export/Copy Turn row kinds and export-title fields (`api::Message` / `api::SessionSummary` lack them; Transcript #15, #16)
- Minimap (afterhours needs basic geometry; WIP in vendor)
- Spotlight (needs .app bundle + LaunchServices; parked in Phase G)
- Horizontal scroll on tabs (afterhours gap #26, addressed in todo.md)

---

# The reference 0.8.3 → 0.8.9 delta (recorded 2026-10-02)

Source: the reference's `CHANGELOG.md` at fbsource master (0.8.3–0.8.8) plus the 0.8.9
release notes relayed to this lane (0.8.9 is not in the file at master yet).
Base: Hanabi batch-2 tip 9b664d0 (app 65382cb), which ported line spacing and
sidebar hysteresis. Each row was checked by reading Hanabi's own source at that
tip; VERIFY means the source read did not decide it and nothing was run.
Status words: MISSING, PARTIAL, MATCHED, N/A (no analogue surface; the row
says why), VERIFY.

| ID | Version | Behavior | Hanabi at 9b664d0 | Status |
|---|---|---|---|---|
| D01 | 0.8.3 | Archive Current Conversation in File menu + Keyboard Shortcuts, unassigned by default; archives without closing the tab, Undo notice; disabled when already archived | none at 9b664d0. Now: File menu + Keyboard Shortcuts command, unassigned by default, archives the focused pane's conversation and keeps its tab, ordinary Undo toast, disabled when archived; `tests/ui/archive_current_conversation_keeps_the_tab.e2e` (lead lane, 2026-10-02) | MATCHED |
| D02 | 0.8.3 | New Conversation can be marked Sensitive where the account allows | "No field" was wrong: the entitlement is the viewer's rollout gate, `viewer { sensitive_mode: if_gk(gk: "agentcloud_sensitive_mode_switch") }` over `/api/graphql` (the reference's SensitiveModeGate; advisory -- the orchestrator admits), and the create carries `options.control.may_add = ["sensitive"]`. Now the gate is read once; where it admits, the New Thread composer shows a "Sensitive: off / may ask" chip (tooltip: the agent may ASK; an approved switch cannot be undone); the choice rides one create and resets; where it does not admit, nothing is drawn and nothing is reserved (`api/companion_wire.h` sensitive_gate_*, `LaunchTuning::allowSensitiveSwitch`, `create_command_json`; test_agentcloud; `new_thread_may_reserve_the_sensitive_ask.e2e`, `new_thread_offers_no_sensitive_choice_when_the_gate_refuses.e2e`; lead lane, 2026-10-02). Mock-verified. Not ported: the in-thread approval card / sub-thread path | MATCHED (create-time reserve; mock-verified) |
| D03 | 0.8.3 | "Open diffs in <the reference>" opens diff links as native tabs | no native diff surface | N/A |
| D04 | 0.8.3 | A Space sets a default theme for its threads | Re-checked (lead lane, 2026-10-02): not a wire fact. The reference keeps Space themes LOCALLY (SpaceThemes in its defaults store) and resolves a thread's theme as its own, else its Space's, else the app's -- on top of PER-THREAD themes (SessionThemes / the rotation), which Hanabi does not have: its theme is one global choice. D04 needs per-thread theming first (a pane-scoped palette), then the Space layer | MISSING (needs per-thread themes) |
| D05 | 0.8.3 | Typing no longer saves a server draft per keystroke | drafts are local only; no server draft write exists | MATCHED by construction |
| D06 | 0.8.3 | Sign-in failures explain themselves and keep the list on screen | the loader already kept a stale list when a refresh failed, but said nothing while it was up (only an empty folder read "could not be read"). Now: one toast per failure streak, "Couldn't refresh the thread list (<the server's words>). Showing the saved list.", cleared by the next success; `tests/ui/a_failed_refresh_keeps_the_list_and_says_so.e2e` with HANABI_MOCK_LIST_FAIL_FROM (lead lane, 2026-10-02). The sign-in sheet's own failures (expired code, offline) were already covered by the auth_* fixtures | MATCHED |
| D07 | 0.8.3 | Recent rows no longer reshuffle on every update | `sidebar_hysteresis.h` (batch 2) | MATCHED |
| D08 | 0.8.4 | Line spacing in Settings → Appearance | `line_spacing.h` (batch 2) | MATCHED |
| D09 | 0.8.4 | Cmd+F can start from the newest match (Settings → Search) | oldest-only at 9b664d0. Now: Settings > Chat "Start from the newest match" (this Mac, default off): each Cmd+F starts at the newest match, Cmd+G walks older, chevrons keep up-older/down-newer; `tests/ui/find_can_start_from_the_newest_match.e2e` (lead lane, 2026-10-02). Hanabi has no Search pane, so the row sits in Chat | MATCHED |
| D10 | 0.8.4 | "@" completes a Metamate Space by name; member picker shows faces | Now threads AND Spaces: `@` (opening a word, query up to 64 chars, spaces allowed) offers root threads -- ranked title-starts / word-starts / contains, freshest first; a bare `@` the freshest -- then the viewer's Metamate Spaces (read once from the web app's `/api/graphql` `metamate_projects`, named agents dropped, pinned then viewer_rank; `api/spaces_wire.h`). A thread pick writes its web URL, a Space pick `space:<id>` (fbid-checked), each plus a space; a row the draft already names is marked; Escape keeps the words (`ui/thread_mention.h`; test_thread_mention, test_spaces_wire; `tests/ui/at_mentions_a_thread_by_name.e2e`; lead lane, 2026-10-02). Still missing: the reference's "Space member picker shows faces" belongs to its Space settings (members, rename, icon, visibility), a surface Hanabi does not have at all -- it is that surface, not a picker tweak; and the typed-vs-pasted trigger rule Now (lead lane, 2026-10-03) the members surface exists: right-click a Space's header -> Space settings… opens the reference's SpaceSettingsSheet -- name and icon (one Save each; the field then shows what Metamate STORED), who can see it (Private / Public; locked with its reason for a SENSITIVE Space), the roster (Metamate's count, admins first; ADMIN and OWNER both admin), Make admin / Make member / Remove per member (not on a member with no Employee arm), Add someone by name (the web picker's intern_typeahead_query, EMPLOYEE_ONLY, a short last token closed; a pick IS the write), Leave Space ("Leave this Space?", naming the viewer's own fbid; off until it is known) and Archive Space ("Archive for everyone?", keyed on is_archived coming back true), Done. What is OFFERED follows the catalog's admin flag; every write is Metamate's mutation and a refusal shows the server's words (operator envelopes and transport noise replaced by the write's own sentence; the visibility control goes back to what the server holds). (`api/space_manage.h`, `ecs/space_settings_system.h`, Client::graphql_field; test_space_manage; `space_settings_rename_roster_and_role.e2e`, `space_settings_add_someone_by_name.e2e`, `space_settings_refusal_and_visibility_go_back.e2e`, `space_settings_member_view_and_leave.e2e`, `space_settings_sensitive_space_cannot_go_public.e2e`; capture 07o.) Then (same day) faces: every member and person row draws a 20pt face before the name -- the person's profile photo (InternGraph `intern_user_for_fbid_or_unixname { profile_picture(40x40) { uri } }`; the signed URL used once and the IMAGE kept on disk for a day; fetched with no credential, only from an https *.fbcdn.net host, through the proxy, 2 MB cap; masked to a circle), else the monogram; the name never waits for the face; four lookups in flight at most, each person asked once a launch (`space_manage::photo_*`, Client::fetch_person_photo, AppComponent::PeoplePhotos; `space_settings_people_show_faces.e2e`, `space_settings_faces_fall_back_to_letters.e2e`; capture 07o). Differences: the Space's emoji does not draw in the icon field (no loaded face carries emoji, afterhours_gaps.md #48); the per-Space theme menu (D04) is not in it. Mock only -- never run against Metamate | MATCHED (members surface; mock-verified) |
| D11 | 0.8.4 | Sidebar search `last_active:` today / yesterday / a date | no operators at 9b664d0. Now: `last_active:today`, `:yesterday`, `:YYYY-MM-DD` narrow the sidebar to threads whose last activity falls on that local day; the other words still match titles and content; an unknown value stays search text (`ui/sidebar_query.h`, test_sidebar_query, `tests/ui/sidebar_search_last_active.e2e`; lead lane, 2026-10-02). 0.6.7's `origin:`/`attention:`/`label:` are still absent | MATCHED (last_active only) |
| D12 | 0.8.4 | Ctrl+Tab / Ctrl+Shift+Tab and Cmd+Option+←/→ cycle tabs (wrapping); Cmd+[ / Cmd+] step without wrapping | only Cmd+1…9 at 9b664d0. Now: six Window-menu commands with the reference's chords, focused-pane scoped, `tests/ui/relative_tab_chords_step_and_cycle.e2e` (lead lane, 2026-10-02); Ctrl Tab needed an Afterhours workaround, gap #608 | MATCHED |
| D13 | 0.8.4 | Native diff shows its dependency graph | no native diff surface | N/A |
| D14 | 0.8.4 | Settings → Memory browses and edits agent memory | none at 9b664d0. Now a Memory pane: the viewer's personal memory (`metamate_personal`) as folders and files -- open a folder and go back up, open a file in an editor, Save (UPDATE_ONLY with the version read, so another writer's change is refused, not overwritten) or Discard, create a file (CREATE_ONLY); a dirty edit holds the page; failures are one plain sentence with Try again. Wire: the web app's `/api/graphql` with the inbox-state credential, variables as a JSON string, both envelopes checked against the folder/key asked (`api/memory_wire.h`, `ecs/memory_page.h`; test_memory_wire, test_memory_page; `settings_memory_browses_and_edits_memory_files.e2e`, `settings_memory_says_when_it_cannot_load.e2e`; lead lane, 2026-10-02). Mock only -- never run against the real service. Not ported: Space memory, folder creation, upload, paging past 100 | MATCHED (personal; mock-verified) |
| D15 | 0.8.4 | Code highlights once it settles; long replies stay visible; pinned status cards stay docked | checked (lead lane, 2026-10-02): code is coloured live, a deliberate difference (the scanner state runs down the block every frame); a long streaming reply stays in view through the bottom anchor on the live row (`TranscriptLedger` follow, resolved after the row is measured); Hanabi renders no pinned status cards (agent Elements), so the third part has no counterpart | N/A (code colour deliberate; no pinned cards) |
| D16 | 0.8.5 | Starts faster by reusing the saved list and recent history | `disk_cache`, `preload.cpp` serve a cached catalogue at launch | MATCHED (not re-measured) |
| D17 | 0.8.5 | Cmd+K no longer redraws the whole window behind the palette | checked (lead lane, 2026-10-02): Hanabi is immediate-mode and redraws the whole window on every PRESENTED frame by design; what it controls is whether a frame is presented (frame activity / idle skip). Not a redraw-scope fix that can be ported; palette2000 is held by alloc_gate | N/A (architecture) |
| D18 | 0.8.5 | Voice-started sessions stay in the background | no voice input | N/A |
| D19 | 0.8.5 | Thread mentions name an untitled thread from what you are typing | the @ picker's rows name an untitled thread (including the wire's "(untitled)" placeholder) from its preview's first line, then "Untitled thread" (`mention::row_title`). A sent thread URL now reads as a link in the bubble (user and assistant text) and a click opens that thread in a tab when this client holds it, the web page otherwise (`mention::find_threads`, `links_in`/`link_hotspot`; lead lane, 2026-10-02). Still not drawn: the URL replaced by the thread's title (the reference's display half; changing the visible text would invalidate measured heights on a rename) Display half (lead lane, 2026-10-02, later): a reference in a USER message is now drawn as `@<current title>` for a thread this client holds (the wire keeps the URL; an unheld one stays its URL), the drawn names ride in the measurement key and a title change bumps the transcript ledger's epoch, so a rename re-measures; the link spans follow the names (`ui/thread_mention.h` titled, `main_pane_system.h` titled_refs/add_title_links; test_thread_mention; `a_mention_follows_a_rename.e2e`, `at_mentions_a_thread_by_name.e2e`). Assistant lines too (their per-line links follow the drawn names) | MATCHED |
| D20 | 0.8.6 | Search results do not flicker or go stale while typing | checked (lead lane, 2026-10-02): the session search queries its index synchronously, every frame, with the CURRENT query (`session_search_system.h`), so no result for an older query can arrive late. Rows can still be added as the index deepens a few transcripts a frame, and the coverage note says how far it has got | MATCHED by construction |
| D21 | 0.8.6 | One conversation cannot appear twice in sidebar or search | not traced at 9b664d0 (replace_sessions kept whatever the list handed it). Now: api::catalog::keep_one_row_per_id on every catalogue replacement, the freshest row kept in its first place; test_session_catalog (lead lane, 2026-10-02) | MATCHED |
| D22 | 0.8.6 | Automation threads raise no run-finished banners and are not in the @ picker | The earlier note here ("the ws `list` row carries no automation/origin flag") was wrong: list rows carry `origin_application` (stamped once at create) and `title_is_human` (skip-if-false), and the reference reads exactly those (AutomationOrigin: `metamate` origin and no human-set title; a pin outranks it). Now parsed, cached on disk, and `api::is_automation_born` decides: an automation's run FINISHING raises no banner or chime unless pinned (blocking still does), and the @ picker never offers one (`util/notify_events.h`, `main.cpp`, `ui/thread_mention.h`; test_notify_events, test_thread_mention; `at_mentions_skip_automation_threads.e2e`; lead lane, 2026-10-02). Mock-verified (HANABI_MOCK_AUTOMATION) | MATCHED (mock-verified) |
| D23 | 0.8.1/0.8.6 | Multi-question card: a page per question, Next, Submit only on the last page, review page | one card listing every question at 9b664d0. Now: a form with 2+ questions pages one question at a time under a tab row (one tab per question, a check when answered, then a Submit tab that is the review page reading every answer back); the primary button is Next on every question but the last and only advances, Submit on the last question and the review page sends the whole form; Return does what the primary says and leaves the caret on the new page's primary; the next-ask button now reads "Next ask"; `tests/ui/a_multi_question_card_pages_one_question_at_a_time.e2e` + 27 ask fixtures ported (lead lane, 2026-10-02). Not ported: option letters, per-option previews, Tab cycling the tab row | MATCHED |
| D24 | 0.8.6 | A parked reader returns to the bottom when a hold releases | checked (lead lane, 2026-10-02): Hanabi's follow latch (`follow_latch.h`) has no hold state -- it breaks on a scroll-up and re-arms when the reader reaches the end -- so there is no hold whose release could strand a parked reader. The reference's diff was not found to compare the exact trigger | N/A (no hold state) |
| D25 | 0.8.6 | When a run folds away, later messages stay steady | checked (lead lane, 2026-10-02): the transcript is anchor-relative (`TranscriptLedger`): a row that folds above the anchor changes the prefix, not the anchored rows, and a followed thread keeps its bottom anchor on the live row. No dedicated fold-at-run-end fixture | MATCHED by construction (unfixtured) |
| D26 | 0.8.8 | New Conversation picker choices easier to read | reference-specific picker styling | N/A |
| D27 | 0.8.8 | Voice recording at the 90 s limit lands in the composer | no voice input | N/A |
| D28 | 0.8.8 | Code blocks show no grey strips between lines | ink check (lead lane, 2026-10-02): t4's 4-line ts fence, column x=900 runs one chip colour (4,7,12) unbroken from y=240 to 302 across the three full-width lines; only the last line hugs its words | MATCHED |
| D29 | 0.8.8 | A new conversation that never gets an answer shows Retry with the prompt intact | checked (lead lane, 2026-10-02): a Hanabi create cannot spin forever -- the create round trip has its own deadline (`kForkTimeoutSecs`) and the first message's post a 120 s read timeout (`create_with_message`), and `create_outcome.h` resolves each of the six outcomes with the text and files back in the composer: Retry where a repeat is provably safe (a refused create), a sentence and NO button where the session may already exist (an unheard answer), deliberately, because that Retry is how a reader gets two conversations. Once created, the working indicator follows the server's own run state, so there is no client spinner to strand. The reference's diff for this fix was not found to compare its trigger | MATCHED by construction (Retry withheld when unsafe, on purpose) |
| D30 | 0.8.8 | Picture artifacts fit the preview | `artifact_viewer_system.h` scales by min(1, box/natural) | MATCHED |
| D31 | 0.8.8 | Scrolling back loads older messages when asked | `load_older_model.h` | MATCHED (not re-run) |
| D32 | 0.8.9 | Cmd+click an artifact opens it on the web | none at 9b664d0. Now: Cmd+click (Ctrl in scripts) on an artifact row opens `<web base>/<session>?dock=artifact:<id>:<version>`, the web app's own page for it (the reference's webAppDockURL); a plain click is unchanged; nothing opens with no web base (`model::artifact_web_url_for`, `tests/ui/cmd_click_an_artifact_opens_its_web_page.e2e`; lead lane, 2026-10-02). Hanabi has no Companion, so the plain click opens no viewer for a non-image row | MATCHED |
| D33 | 0.8.9 | Sidebar sort: Oldest first | activity order only at 9b664d0. Now: Settings > General "Sort threads by" Recent activity / Oldest first; oldest created first, unknown last, id tie-break, pin still lifts, every section; creation times learned per session (attach + the seq-1 frame's `created_at_unix_ms`, since the `list` row carries none) while the order is chosen, kept on disk; `tests/ui/sidebar_sort_oldest_first.e2e` (lead lane, 2026-10-02). Deviation: the reference puts it in the sidebar list-options menu; Hanabi's filter glyph is a one-state toggle, so the choice lives in Settings | MATCHED |
| D34 | 0.8.9 | Files-changed chip and panel | none at 9b664d0. Now: a "N files" chip in the composer strip, folded from APPLIED edit/write calls (`api/session_changes.h`: one file per node:path, newest first, line diff with context and an LCS budget), opens a panel: each file with +adds/-dels, a file's edits as diffs, and its whole text when the last write and every later edit place exactly; a failed edit is not a change. Edit/write calls now keep their raw input (`Message::tool_input`). test_session_changes, `tests/ui/the_files_changed_chip_opens_a_panel_of_diffs.e2e` (lead lane, 2026-10-02). Not ported: Copy, markdown rendering of a whole .md file | MATCHED |
| D35 | 0.8.9 | Saved templates in the / menu | built-ins only at 9b664d0. Now: Settings > Commands > Saved templates (name + text, Remove; built-in names refused with a reason; machine-local); `/name` offers them after the verbs, picking or Entering puts the text in the box and sends nothing; `tests/ui/composer_saved_templates.e2e` (lead lane, 2026-10-02). Deviation: no TEMPLATES header row; each template row says "Template" where a verb would say Unavailable | MATCHED |
| D36 | 0.8.9 | Bug-report window captures the window | none at 9b664d0. Now Help > Report a Bug (unassigned chord): the window is captured as the sheet opens (so the picture is what the reader saw), and the sheet shows what will be sent -- text (line 1 the title), board (Hanabi / Agentcloud, each saying what it is for and where it files), the capture on or off, the exact context lines. Filing runs `meta knots.issue create` as the reporter (argv-only, 30 s bound), uploads the capture first with `meta phabricator.file upload` and links it (Knots renders no images), joins the namespace once on a membership refusal; a refusal keeps the sheet and its words, a success closes it with a Copy for the link (`api/knots_reporter.h`, `api/knots_runner.h`, `ecs/bug_report_system.h`; test_knots; `report_a_bug_*.e2e`; lead lane, 2026-10-02). Every scripted and mock run files into a fake runner; an E2E build refuses to spawn `meta` at all. App board gabeochoa/manager (settled by the coordinator, 2026-10-02; env-overridable), backend board agentcloud/client | MATCHED (mock-verified) |
| D37 | 0.8.9 | Companion unified diff | Hanabi had no Companion. Now a click on a D-number link opens the Companion in-app (Cmd/Ctrl-click still opens the browser): title, status, repo, version, the files with line counts; a file opens to EVERY comparison hunk as one unified diff -- a range header per hunk, context, removals and additions interleaved; reads are the reference's own `/api/graphql` queries (phabricator_diff_query, phabricator_version changesets), each answer checked against the number/version/path asked (`api/companion_wire.h`, `ecs/companion_system.h`; test_companion_wire; `companion_reads_a_diff_as_one_unified_diff.e2e`; lead lane, 2026-10-02). Mock-verified only. Not ported: stacks, review, inline comments, actions, monospace code | MATCHED (unified diff; mock-verified) |
| D38 | 0.8.9 | Companion task Comments page | Now a click on a T-number link opens the Companion on the task: Overview (status, priority, owner, filer, tags, description) and Comments -- the newest ten via `intern_activity_comments(first: 10, exclude_deleted: true)`, author, when, plain text -- in their own read, so a miss fails that page and never the card (`companion_task_has_a_comments_page.e2e`, `companion_comments_fail_alone.e2e`; lead lane, 2026-10-02). Mock-verified only. Not ported: Related, status/assign/comment actions | MATCHED (comments; mock-verified) |
| D39 | 0.8.9 | `/knot` in the composer | none at 9b664d0. Now `/knot <what needs doing>` in a thread files a knot as the reporter, linked the web capture's way (`sessionId` metadata + a `Session: <id>` description line); the composer says which knot it became with a Copy for its link; an empty or session-less /knot says what is missing; a refusal hands the command back (`knots_system.h`; `slash_knot_*.e2e`; lead lane, 2026-10-02). Same fake-runner safety as D36 | MATCHED (mock-verified) |
| D40 | 0.8.9 fix | Credential-refused images retry | artifact reads gave up on a 401 and the 15 s transient retry reused the same cached token. Now: a 401 drops the token and retries once with a fresh mint; second 401 final; 403 not retried (as D122768089); test_agentcloud (lead lane, 2026-10-02). Local inline images (`inline_image.h`) carry no credential and are unaffected | MATCHED |
| D41 | 0.8.9 fix | An unreadable file does not block a message | the reference's fix (D122768094) sends an unsupported kind as a mention of its name, this Mac and its path instead of holding the message. Hanabi refuses an unsupported or unreadable file at INTAKE with the reason (never stages it). A staged file that becomes unreadable before the send (moved, deleted, emptied) now goes as a line `[<name> could not be read from <path> when this was sent]` after the words, and the readable files still upload; over the size cap is still a refusal (`message_request_json`; test_agentcloud "an unreadable file does not hold the message"; lead lane, 2026-10-02) | MATCHED |
| D42 | 0.8.9 fix | A queued message is not sent while it is being edited | sends queued but a queued message could not be edited. Now the queued edit exists and is held: Up from the draft lands on the newest queued (unsent) message, then older ones (then what was sent); Return rewrites it in place -- same place in the queue, the outbox copy rewritten -- and the draft the walk started from comes back ("Queued message updated"); Down past the newest goes back to the draft; the caption reads "Editing a queued message · Return saves it · ↓ goes back"; while one is open its thread's queue HOLDS, so a turn finishing does not send it (or anything behind it) out from under the edit; a draft with files staged cannot step onto one and says so (the reference's wording). Hanabi queues locally, so the reference's server limit (a run consuming a queued input at its own boundary) does not arise (`AppComponent::queue_held`, PaneState::editingQueued; `a_queued_message_can_be_edited_and_holds_the_queue.e2e`, `leaving_a_queued_edit_releases_the_queue.e2e` with a held mock stream; lead lane, 2026-10-03) | MATCHED (mock-verified) |
| D43 | 0.8.9 fix | Large-attachment staging writes only changes | attachments go inline base64; no staging store | N/A |
| D44 | 0.8.9 stack (D122950174) | Update ready: the existing one-line banner strip says a newer build is installed and waiting on a restart (MSC ManagedInstallReport, checked at launch / every 45 min / on activation); Restart and Later (Later dismisses that version only) | Hanabi is not an MSC package; its analogue is "the app on disk is not the one running" (an install replaced the bundle or a rebuild replaced the binary). Now: the executable's identity (inode, mtime, size) is read at launch and every 30 s (`util/update_ready.h`); while it differs, one line above the sidebar footer reads "Update ready" with Restart (a detached `open` of the bundle -- or the bare binary -- a second later, then the normal quit) and Later (hides it for that on-disk build; a newer one shows again); a file missing mid-install says nothing (test_update_ready; `update_ready_strip_restart_and_later.e2e`, `no_update_strip_when_the_running_build_is_on_disk.e2e`; capture 07b; lead lane, 2026-10-03). Restart is never exercised in a scripted build | MATCHED (analogue) |
| D45 | 0.8.9 stack (D122951583) | Every sidebar row is the one session row, so a Space's guests show a mark and an age; a Space header never reads "loading…" over rows already listed | Space sections (fe2502f/86671de) draw through the one row renderer, so guests carry the mark and age; Space headers have no loading state at all | MATCHED by construction |
| D46 | 0.8.9 stack (D122952134) | New Chat's Nodes chip and Space filter open in-window instead of a separate popover window (which took seconds on a managed Mac) | every picker is drawn in-window; Hanabi has no separate popover windows | MATCHED by construction |
| D47 | 0.8.9 stack (D122961363) | An open run's detail block (usage, cache, calls, span, cost) moves behind "Show run details (debug)", off by default; a run shows only its working/elapsed line | Hanabi never drew an inline run-detail block: a live run shows the working dot and elapsed seconds; token usage lives in the context meter's popover, opened on demand | MATCHED by construction |
| D48 | 0.8.9 stack (D122984730) | A paragraph under a list sits a paragraph's gap below the last item | a blank source line drew the paragraph gap (kBlankPitch), but a line directly under a list item sat tight against it. Now the rich body gives it the paragraph gap: `ui/md_list_gap.h` inserts the blank line the author left out, between a list item and an unindented non-list line (an indented line is the item's continuation; nothing inside a ``` fence is touched), applied by both rich_body_h and render_rich_body so measure and draw agree, and allocation-free when no gap is owed (test_md_list_gap; lead lane, 2026-10-03) | MATCHED |
| D49 | 0.8.9 stack (D122987004, D123014515) | Agent Memory save/create errors that a retry cannot fix (file changed, name taken, may not write) say what to do; a refused credential is dropped and retried once, and "reopen" is said only if the fresh one is refused | Memory pane failures were one generic sentence per action ("try again in a moment"). Now `api/memory_wire.h` classify_write / classify_read (the reference's table: status first -- 403 or PROTECTED_MODE read-only, 401 sign-in expired, 5xx the service -- then the service's words: VersionConflict, AlreadyExists, Permission denied) map a refusal a retry cannot fix to what to do (reopen and reapply; pick another name; read-only for you; quit and reopen; no access); anything else keeps "try again". A 401 drops the web credential and the call is made once more with a fresh one before it counts (`memory_post`). The draft is never touched (test_memory_page table + denied save; `a_memory_save_the_reader_may_not_make_says_read_only.e2e`; lead lane, 2026-10-03) | MATCHED (mock-verified) |
| D50 | 0.8.9 stack (D122992893) | The first launch of a newer build opens Settings on its Changelog, at that version's section, once per version | Hanabi keeps no release notes (it is built from git, not released); there is nothing to open | N/A (no changelog source) |
| D51 | 0.8.9 stack (D123023215; base D122878899) | MM3: a third icon set whose faces (rectangle path data from the MM3 Animation Library) mark rows, the menu bar and search; the stack adds Blocked / Review / Pinned / parent faces and animation | Hanabi had one icon set. Now Settings > Appearance > Icons: Normal / MM3. MM3 draws the source rectangles of the landed faces (`ui/mm3_faces.h`, copied from the reference's MM3Face) on the reference's state mapping: working blinks, asking waves, done winks, a settled row is tired; blocked, frozen and paused keep their conventions; sidebar rows and tabs alike (test_mm3_faces; capture 07m; lead lane, 2026-10-03). Not ported: the menu-bar face, the shelf icons (home/archive/search faces), and the stack's new Blocked / Review / Pinned / parent faces and animation (D123023215 is unpublished; its art is not on master). At 1x a face is ~12x8 px with no antialiasing (afterhours_gaps.md #92) and reads as a rough face; on a Retina display it has twice the pixels **Ported from D123162830 (landed 2026-10-03 15:14 ET as 50cf505; its MM3Face.swift, IconTable.swift and SessionDial.swift are byte-identical to draft 31578e3, the source ported) (and its base D123023215), 2026-10-03, lead lane -- re-check when it lands:** rows waiting on you (asking, blocked) loop the library's whole angry animation end to end (39 frames over 2 s, no rest) and keep moving behind another app, still faces wave / tired; a failed run is the whole angry face, still; a live run plays the library's blink calmly (1.4 s, then a 4.5 s rest) and stops while another app is in front; idle is the resting smile; every face draws whole in the row box (5,6 123x99) at one 8pt frame on rows and tabs; a hidden window, the screens asleep or Reduce Motion stills every face (`ui/mm3_anim.h` frames + timing decoded from the reference's tables, `ui/mm3_marks.h` mapping + gates, `status_mark::draw_mm3`; a moving face that drew keeps the frame loop at the active cadence, a still or off-screen one costs nothing; test_mm3_marks; `mm3_rows_waiting_on_you_loop_angry.e2e`, `mm3_faces_are_still_when_the_window_is_hidden.e2e`, `normal_icons_never_animate.e2e`; capture 07m re-taken). Then (same day): the shelf rows wear the faces under MM3 -- Home smiles, Blocked is tired, Review winks, Pinned is the heart (its own box); Archived and Settings keep their marks (`mm3::shelf_face`, `status_mark::draw_face_boxed`; capture 07m) -- and the menu bar's status item wears the resting face as a template image with the blocked count beside it instead of the spark (`menubar_set_mm3`; a windowed mock launch logged `menubar: mm3 face 19.0x16.0 template=1`; not photographed: screen capture is refused on Aspen). Then the menu bar MOVES (the reference's MM3MenuBar, same draft): its face says what the catalog adds up to -- idle smiles and blinks now and then (only while Hanabi is in front), a run blinks every 4-6 s, a thread waiting on you waves every 6-8 s, an unreadable catalog is tired (played once, then held); one NSTimer at most and only while a frame or a play is due, none at rest, under Reduce Motion, with the screens asleep or the session switched away, or under Normal; a frame whose pixels at the screen's scale match the one showing is not handed over (`ui/mm3_menubar.h` plan, step walk and pixel key; smileBlink / wave / tired decoded into `ui/mm3_anim.h`; test_mm3_menubar; a windowed mock launch with needs-you rows logged the mood and 35 frame swaps for one 43-frame wave in 7 s). Still not ported: the unseen row's turned face (Hanabi has no unseen mark) | MATCHED (both landed 2026-10-03: D123023215 at 10:32 ET as be1c02e -- its MM3MenuBar.swift identical to the version ported; D123162830 at 15:14 ET as 50cf505 -- byte-identical to the draft ported) |
| D52 | 0.8.9 stack (D123026216) | A message sent after a run ended or was interrupted gets the working marker under it at the bottom, not on the previous run's header | checked (lead lane, 2026-10-03), and re-checked against the landed source (2217b275, 15:14 ET): its change is to which run HEADER the reference's grouping calls live (`lastGroupID` now stops at a human input older than the open run). Hanabi's working mark is the PLACEHOLDER of the in-flight turn's own first line (`render_thinking_indicator`, started by `drive_stream` on the send), appended after the message just sent, and no run header or pile ever carries a live mark or an open-while-working default -- piles open only when the reader opens them (`expandedPiles`). By code reading: the mock delivers a reply's text at once, so it cannot hold a send in the thinking state for a script to measure | MATCHED by construction |
| D53 | 0.8.9 stack (D123026764) | A `kt-xxxx` in a message draws as a link-preview card (id, title, status, priority, namespace, type), like diffs and tasks | Hanabi linked D/T ids and drew no link-preview cards. Now a reply naming a diff, task or knot draws a card under it -- PowerTools' layout: the id (mono), a task's or knot's priority badge then the status badge (four tones), the title, a footer (board, type, owner, "Created 3d ago"), capped at 320px; a click opens the card's page (`api/link_preview.h` refs_in / decode / decode_knot / the badge words ported from linkPreviewManager.js; Client::fetch_link_preview: diffs and tasks through `/api/graphql` `xfb_gchat_powertools_json_config(getLinkPreview)` with the URL built from matched digits, knots through `meta knots.issue locate` + `get`; `service_link_previews` scans the newest 200 rows of each open thread when they change and once a minute, fetches at most 8 at once, caches by the server's TTL, a failure keeps a good card on a 10-minute clock; test_link_preview; `diff_task_and_knot_references_draw_as_cards.e2e`, `two_references_stack_and_a_failed_lookup_draws_nothing.e2e`, `no_card_when_the_preview_service_does_not_answer.e2e`; capture 07n; lead lane, 2026-10-03). Differences: 3+ cards show a count and the first two with "Show all N" (the reference scrolls the run sideways; afterhours has no nested horizontal gesture), the title is one line, assistant replies only (not user bubbles), and the cache is not kept on disk. Mock-verified; never run against the real service | MATCHED (mock-verified) |
| D54 | 0.8.9 stack (D123028453) | The Last active date filter moves from its own chip into the list options menu (Any time / Today / Yesterday / Choose date, same `last_active:` token) | `last_active:` worked only when typed (D11) and the filter button toggled automated rows. Now the filter button opens a List options menu: Hide automated threads, then a Last active section -- Any time / Today / Yesterday / Choose date… -- writing the same `last_active:` token the field reads (the rest of the query kept word for word; `sidebar_query::with_last_active` / `last_active_value`), the row in force ticked; Choose date… is a prompt that takes YYYY-MM-DD and refuses an impossible day ("Use a date like 2026-10-02."). Difference: no date-picker calendar (Hanabi has none) (test_sidebar_query; `last_active_from_the_list_options_menu.e2e`; lead lane, 2026-10-03) | MATCHED |
| D55 | 0.8.9 stack (D123030851) | One archive write per session in flight; a newer intent replaces a queued one, so Archive then Unarchive cannot land the stale archive last; a failed write restores the prior local value | archive writes (9bde6af) were queued per gesture and ran concurrently. Now one archive write per thread is in flight; a newer intent waits and replaces an older waiting one (keeping the oldest prior); a refused write puts the overlay back to what it was before the gesture (none for a web-only archive) and says "Not archived: the server did not take it" (`archive_then_unarchive_lands_in_order.e2e` with a held mock write, `a_refused_archive_puts_the_thread_back.e2e`; lead lane, 2026-10-03) | MATCHED (mock-verified) |
| D56 | 0.8.9 stack (D123033289) | Keys typed across two quick thread switches follow the latest keyboard selection in their window | Hanabi has no type-ahead buffer across thread switches; keys go to the focused field each frame | N/A |
| D57 | 0.8.9 stack (D123050255) | Settings' default autocompact row names the agentcloud boundary; the last-read boundary is kept on disk | Hanabi has `/autocompact` per thread and no default-autocompact setting | N/A (no setting) |
| D58 | 0.8.9 stack (D123051693) | A thread archived (or unarchived) on the web leaves (or returns to) Recents when its fleet delta lands | Hanabi reads archive state from the session list on each list fetch (no fleet deltas); a web archive lands on the next fetch. A thread archived or unarchived HERE used to keep a local override forever; now, once our write lands on the server, the overlay is dropped and the row carries the server's stamp, so a later change on the web reaches it on the next fetch (`a_web_unarchive_reaches_a_thread_archived_here.e2e`; lead lane, 2026-10-03) | MATCHED (by list fetch; mock-verified) |
| D59 | 0.8.9 stack (D123054059) | Chart x-axis labels keep the narrow ones that fit when the first cannot sit beside the last | Hanabi draws no charts | N/A |
| D60 | 0.8.9 stack (D123069432) | The Screen Recording refusal's permission button is a real button and names a competing copy | Hanabi has no screen capture (Knots kt-8uce parked on the owner) | N/A |
| D61 | 0.8.9 stack (D123151112, D123127084) | A catalog install that changes no drawn row does not re-run the sidebar; the catalog poll walks the fleet's head, not 60 pages | sidebar buckets rebuild only on a catalogue revision change (sidebar_buckets.h) | MATCHED by construction (perf, not re-measured) |
| D62 | 0.8.9 stack (D123167028, landed 2026-10-03 15:14 ET) | MM3 moments: a review row winks at the calm cadence; a row whose run really finishes waves once (the library's one-handed wave) then wears its settled face -- only a completion waves (asking, blocked, failed and a still-open run do not); an idle row untouched for a day turns tired | Ported (lead lane, 2026-10-03): `ui/mm3_marks.h` (`is_completion`, `RunPhase`/`finishes` on status AND own-run-open, `stale_now`, `Mark::once`), the wink animation decoded into `ui/mm3_anim.h` (`frame_once` holds the last frame), `ui/status_mark.h` `moving_frame` (a once-play on its own clock, waking the loop at its next frame and at its end), `SidebarSystem::row_moment` (a per-row memory kept only for rows being drawn, so a row scrolled away and back never waves late). The wave plays behind another app like the urgent rows; Reduce Motion and a hidden window still it. Heart-blink decoded for D63. Tests: test_mm3_marks; `mm3_a_finished_run_waves_once_then_settles.e2e`, `mm3_quiet_paused_frozen_and_day_old_rows_have_faces.e2e`; capture 07m re-baselined. Hanabi has no unseen state, so the reference's unseen arm has no row here | MATCHED (mock-verified) |
| D63 | 0.8.9 stack (D123182666, landed 2026-10-03 15:14 ET) | MM3: Home and Pinned blink in the VIEWS list; GENERATED faces (in the library's grid, not from its sheet) for a live run quiet 3 minutes (thinking: eyes up, three dots), a paused row (asleep, a z rising) and a frozen row (confused, eyes swapping sizes) | Ported (lead lane, 2026-10-03): thinking / sleeping / confused animations and the sleeping / confused stills decoded into `ui/mm3_anim.h` / `ui/mm3_faces.h` (the GENERATED note kept); `Anim::loops` (a cycle never rests) and per-animation chrome boxes; `mm3::thinking_now` needs the row's OWN run open (its last event is its own clock); `status_mark::draw_shelf_face` blinks Home's smile and Pinned's heart in their own boxes at the calm cadence. Tests: test_mm3_marks; `mm3_quiet_paused_frozen_and_day_old_rows_have_faces.e2e`; capture 07m re-baselined | MATCHED (mock-verified) |

Tally (43 rows) at 9b664d0: 18 MISSING (6 large or blocked on a wire fact),
1 PARTIAL, 6 MATCHED, 11 VERIFY, 7 N/A. Rows closed since are marked in place. Work order in this lane: D12, D01, D09, D33,
then D39/D35 and D23; VERIFY rows are decided as their areas are touched.

---

END OF GAP ANALYSIS
