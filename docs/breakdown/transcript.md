# Transcript & Rendering Implementation Breakdown

## Verification Summary

**Gaps examined:** 14 (Transcript & Rendering section of puffin_gaps.md, lines 180–336)

**Already built:** 5 gaps were verified as shipped or partially shipped
- Gap #1: Find-in-transcript (Cmd+F, highlight bands, selection) — fully shipped with three e2e tests
- Gap #2: Tool nested sub-rows — fully shipped (`tool_sub_row` renders per-node breakdown with icon, label, duration, status)
- Gap #6: Inline code styling — already renders backtick content in accent color via `md_to_spans`
- Gap #8: Code blocks — fenced code blocks render as sunken monospace rows (no syntax highlighting yet)
- Gap #9: Pipe tables — fully shipped (detects, scans, renders as bordered grid with header band)

**Real gaps to build:** 9 features are genuinely missing

**Total chunks:** 11 independently shippable commits


---

## Gap Analysis: What's Actually Missing

### Real Gap #1: Date dividers (timestamp rows)
**Status:** Completely missing. Currently only time shown per-row (e.g., "3m", "2h").

### Real Gap #2: Thinking disclosure (collapsible rows)
**Status:** Partial. Internal label renders ("Thinking…") with pulsing dot indicator, but no separate collapsible row or visibility toggle. Content is not shown separately.

### Real Gap #3: Minimap navigator (right-edge rail)
**Status:** Completely missing. Only standard scrollbar from afterhours.

### Real Gap #4: Tool row fold defaults (disclosure open/closed)
**Status:** Completely missing. Tool rows always expand. No per-session fold mode (fold all / expand all / auto).

### Real Gap #5: Message delivery status rows
**Status:** Completely missing. Delivery frames exist in data layer but are not rendered as separate row type.

### Real Gap #6: Syntax highlighting in code blocks
**Status:** Completely missing. Code blocks render as plain monospace, no per-language coloring.

### Real Gap #7: Link auto-detection (work-tracker references)
**Status:** Completely missing. Patterns like `D123456`, `T456`, `S789` are plain text, not clickable.

### Real Gap #8: Markdown headers H1–H4 with hierarchy
**Status:** Completely missing. Headers are rendered as plain text, no size/weight variation or accent color.

### Real Gap #9: Streaming animation (pulsing dots)
**Status:** Completely missing. No animation while message streams in. The pulsing dot only exists for "Thinking…" label, not for the message body itself.


---

## Implementation Plan: 11 Chunks

### Chunk 1: Date dividers for messages

**What ships:** When a message is >4 hours older than the previous one, a thin grey divider row appears above it (e.g., "Monday, August 19"). Subsequent messages that same day omit the divider. Time still shows per-row below the divider.

**UX flow:** Empty state: no dividers (first message has no divider above). Single-day thread: no dividers anywhere. Multi-day thread: dividers appear once per day, stacked above the older message. Old history loading: dividers adjust as new messages load above.

**Where it goes:** `src/ecs/main_pane_system.h` — new row type in the transcript loop. New helper `should_show_date_divider(prev_msg, curr_msg)` to detect 4-hour boundary. New method `render_date_divider()` to draw the thin row.

**Dependencies:** Requires message timestamps (`api::Message::created_at` already available).

**How it is proven:** Screenshot showing a multi-day thread with dividers at appropriate boundaries. E2E test: `tests/ui/transcript_date_dividers.e2e` — load a thread with messages spanning 2+ days, verify divider renders above each new day.

**Height synchronization:** The divider is a fixed-height row (~24px). `rich_body_h` and `render_rich_body` already account for non-message rows (tool rows, headers), so add divider height to the measure path in `rich_body_h` at the same point where `is_table_start` and `is_code_fence` branches are handled.

---

### Chunk 2: Thinking disclosure (collapsible, visible content)

**What ships:** Thinking row renders as a separate collapsible section ("Thinking") below the assistant message. Closed by default (▶ arrow, not ▼). Click to expand and read the reasoning. Content is the raw thinking text from the backend.

**UX flow:** Initial: thinking row appears as a collapsed chip. Click arrow: expands to show full thinking text in a monospace/lighter-colored panel. Scroll with the transcript. Close: arrow returns to collapsed state. No visible/hidden toggle in composer.

**Where it goes:** `src/ecs/main_pane_system.h` — new row type, add to main loop after each assistant message that carries thinking. Add to `AppComponent` a `thinking_expanded: map<message_id, bool>` to track per-message fold state. Use existing pattern from tool-row disclosure.

**Dependencies:** Data layer must emit `thinking` field in `api::Message` (check if it already does; if not, this chunk is blocked). Measurement functions must account for thinking row height.

**How it is proven:** Screenshot showing thinking row collapsed and expanded. E2E test: `tests/ui/thinking_disclosure.e2e` — load a thread with thinking, verify row renders collapsed by default, click to expand, verify content shows.

**Height synchronization:** Thinking row is a fixed header + wrapped text body. Add conditional logic to `rich_body_h` that checks for thinking content and accounts for either collapsed-height (24px) or expanded-height (measure the text wrap).

---

### Chunk 3: Fold/disclosure defaults for tool rows

**What ships:** Tool rows render collapsed by default (▶ arrow). Three composer chips allow per-session override: [Fold All] [Expand All] [Auto]. Override state persists per-session. Default is Fold.

**UX flow:** New thread: tool rows collapse by default. User clicks [Expand All]: all tool rows expand, arrow becomes ▼. Click [Fold All]: all collapse. Click [Auto]: tool rows show first result if <200 chars, collapse if longer. Mode persists on that session even after refresh.

**Where it goes:** `src/ecs/main_pane_system.h` tool rendering, `src/ecs/composer_system.h` for the three chips, `src/ecs/components.h` `SessionState` to store fold mode, `src/ecs/thread_model.h` to load/save fold mode per session.

**Dependencies:** Session state must track fold mode (e.g., `enum FoldMode { Fold, Expand, Auto }`).

**How it is proven:** Screenshot showing collapsed and expanded tool rows via chips. E2E test: `tests/ui/tool_fold_modes.e2e` — open a thread, verify tool rows are collapsed by default, click each chip, verify state persists across navigation away and back.

**Height synchronization:** Already handled by existing `is_folded()` check in `tool_pile_height()` — the conditional logic for measuring collapsed vs. expanded tool rows is in place; this chunk wires the UI to control it.

---

### Chunk 4: Message delivery status rows

**What ships:** When a user sends a message, a delivery-status frame arrives before the assistant reply. It renders as a separate row: "[checkmark] Delivered · 12:34 PM", optional spinner if still processing. User sees confirmation the message was received.

**UX flow:** User sends. Immediately appears in transcript as a user message bubble. Below it, a small delivery status row appears. Seconds later, delivery frame arrives; the row updates to show ✓ Delivered (spinner removed). Status row then vanishes when assistant reply arrives (or remains as a visual beat, depending on UX decision).

**Where it goes:** `src/ecs/main_pane_system.h` — new row type in the transcript loop. Detect `api::Message::role == Role::Delivery` (or check for delivery frame type). Render as a small centered row with icon + label + optional spinner.

**Dependencies:** Data layer must emit delivery frames as separate `api::Message` entries with `role == Role::Delivery`. Check `thread_model.h` to see if this field exists.

**How it is proven:** Screenshot showing user message followed by delivery status row. E2E test: `tests/ui/delivery_status.e2e` — send a message, verify delivery row appears and updates.

**Height synchronization:** Fixed-height row (~28px). Add to measure path similar to dividers.

---

### Chunk 5: Syntax highlighting for code blocks

**What ships:** Fenced code blocks render with per-language syntax coloring. Support: Python, Bash, JavaScript, TypeScript, Go, Rust, C++, Java, SQL, YAML, JSON. Line numbers optional (not shown initially, feature-gated).

**UX flow:** Code block appears with a language tag (e.g., "python"). Text is colored: keywords in one color, strings in another, comments in grey. Colors pull from the theme's syntax palette (adding 8 new tokens to `src/ui/theme.h`).

**Where it goes:** `src/ui/syntax_highlighter.h` — new lightweight highlighter (200–300 lines, regex-based per language). `src/ecs/main_pane_system.h:render_code_block()` — swap plain label for styled spans built from the highlighter.

**Dependencies:** None beyond adding theme tokens. Highlighter is self-contained.

**How it is proven:** Screenshot showing a Python block with keywords colored, strings another color, comments grey. E2E test: `tests/ui/syntax_highlighting.e2e` — load a thread with code blocks in 3+ languages, verify colors are applied per language.

**Height synchronization:** Syntax coloring does not change layout — the text content is identical, wrapping is identical. No change to `rich_body_h` needed.

---

### Chunk 6: Markdown headers (H1–H4 with hierarchy)

**What ships:** Headers (# through ####) render with decreasing font size and weight. H1 largest and boldest, H4 smallest. Color: theme accent (Triforce gold). Headers appear with top/bottom margin (not inlined with body text).

**UX flow:** Markdown body contains "# Heading". Parser detects it. Renders as a standalone line in large accent-colored text, with vertical space above and below. Subsequent H2/H3/H4 render smaller but same approach.

**Where it goes:** `src/ecs/main_pane_system.h` — in the body-scan loop, detect header lines (lines starting with `#` at column 0). Add a new row type for each header level, or a single row type with a level parameter. Call `render_markdown_header(level, text)`.

**Dependencies:** None. Markdown parsing already scans lines; just need to detect header syntax.

**How it is proven:** Screenshot showing a message with H1/H2/H3/H4 headers in different sizes and colors. E2E test: `tests/ui/markdown_headers.e2e` — load a message with `# H1\n## H2\n### H3\n#### H4`, verify each renders at correct size and color.

**Height synchronization:** Headers are fixed-height rows (H1 ~32px, H2 ~28px, H3 ~24px, H4 ~20px, plus margins). Add to `rich_body_h` scan logic similar to tables/code blocks.

---

### Chunk 7: Streaming animation (pulsing dots)

**What ships:** While a message is streaming in (before complete), a pulsing dot animation appears above or trailing the first line. Three dots pulse in sequence (`.` → `..` → `...` → repeat) at ~1–2 Hz.

**UX flow:** Assistant starts typing. Message appears in transcript with a few words. Above the text (or trailing the last word), three dots pulse. Animation continues until message is complete. When complete, dots vanish.

**Where it goes:** `src/ecs/main_pane_system.h:render_message_body()` — add a conditional check for `AppComponent::StreamPhase::Streaming` or equivalent. If streaming, draw pulsing dots via `on_draw_fg` callback using elapsed time from the frame clock.

**Dependencies:** Must have a way to query whether the message currently on screen is still streaming (check `AppComponent` state).

**How it is proven:** Screenshot showing message with pulsing dots. E2E test: `tests/ui/streaming_animation.e2e` — trigger a streaming message, screenshot at 3 points (dot phase 1, 2, 3), verify animation loops.

**Height synchronization:** Dots are overlaid, not part of the text flow. No layout impact.

---

### Chunk 8: Link auto-detection for work-tracker references

**What ships:** Patterns in message text become clickable links. Supported patterns: `D123456` (task), `T123456` (task variant), `S123456` (incident). Cmd+click opens in system browser. Host is read from settings (never hardcoded).

**UX flow:** Message contains "See D123456 for details". The identifier renders as underlined blue text. Hover shows a pointer cursor. Cmd+click opens the browser to `{configured_host}/D123456`. No in-app link preview.

**Where it goes:** `src/ecs/main_pane_system.h:md_to_spans()` or a new post-processing pass — regex detect `[DTS]\d{6,}` in the visible text, mark as clickable spans. On click, extract the pattern, build the URL, call `open_url_in_browser()`.

**Dependencies:** Must have a settings field for the tracker host (e.g., `app.trackerHost`). Must be able to open URLs (likely via a native call).

**How it is proven:** Screenshot showing a D-number as a blue link. E2E test: `tests/ui/auto_link_detection.e2e` — load a message with `D123456` in the text, verify it renders as a link, Cmd+click opens the browser.

**Height synchronization:** No impact — links are inline.

---

### Chunk 9: Thinking row: filtering from find-in-transcript

**What ships:** When Cmd+F find is active, thinking rows are excluded from the search (they're skipped). The filter is a one-liner in `find_highlight.h`.

**UX flow:** User opens find, types a query. Thinking rows do not show highlight bands even if they match. Results count and match navigation skip thinking rows. Reasoning is internal; users search the conversation, not the reasoning.

**Where it goes:** `src/ui/find_highlight.h` — add a row-type check in `is_searchable(row)` function. Return false for thinking rows.

**Dependencies:** Chunk #2 must ship first (thinking rows must exist to filter them).

**How it is proven:** Screenshot showing find-in-transcript with highlighting, no bands in thinking rows. E2E test: `tests/ui/find_exclude_thinking.e2e` — load a thread with thinking and matching text in thinking, open find, search for a word in the thinking, verify no highlight appears on thinking row.

**Height synchronization:** No layout impact.

---

### Chunk 10: Minimap navigator (right-edge rail)

**What ships:** Right edge of transcript has a thin scrollable navigator rail. Marks show 5 types: machinery (grey), reply (blue), delivery (green), notice (orange), ask (red). Hover expands rail to full width with labels. Click a mark to jump to that row. Drag the scrubber handle to seek. Settings → Chat Behavior controls which mark types are shown.

**UX flow:** Thread loads. Right edge shows a thin 8px rail with colored dots stacked vertically, each dot representing a message/event type. Hover the rail: it expands to 60px wide, shows labels ("Reply", "Tool running", etc.). Click a dot: transcript scrolls to that message. Settings checkbox toggles visibility per mark type.

**Where it goes:** New file `src/ui/minimap.h` with mark-detection logic and rail rendering. `src/ecs/main_pane_system.h` — instantiate the minimap in the transcript render path, pass the list of messages. Integration with scroll sync (detect when transcript scrolls, update scrubber position; detect scrubber drag, apply scroll).

**Dependencies:** Requires scroll-sync plumbing with afterhours scroll component (measure content height, detect scroll position, apply scroll on click/drag). Also requires `Settings` field for mark-type visibility toggles.

**How it is proven:** Screenshot showing minimap rail on the right, expanded on hover, with multiple colored marks. E2E test: `tests/ui/minimap_navigator.e2e` — load a long thread, hover the rail, click a mark, verify transcript scrolls to that message.

**Height synchronization:** Minimap is positioned absolutely on the right edge, does not affect content width or message layout.

---

### Chunk 11: Settings for transcript behavior (optional, gating feature flags)

**What ships:** New Settings pane "Chat Behavior" with toggles:
- "Show timestamps" (default: on) — gate timestamp display
- "Show minimap" (default: off) — gate minimap visibility
- Checkboxes for minimap mark types (machinery, reply, delivery, notice, ask)

**UX flow:** Settings → Chat Behavior. Toggle "Show timestamps" off: timestamps vanish from all rows. Toggle "Show minimap" on: minimap appears on the right edge. Uncheck "Deliver messages": delivery marks don't appear on minimap.

**Where it goes:** `src/ecs/settings_system.h` — new settings section. `src/ui/theme.h` or settings struct — fields for these toggles. `src/ecs/main_pane_system.h` — conditional render based on these flags.

**Dependencies:** Requires chunks #1 (date dividers), #4 (delivery rows), and #10 (minimap) to ship first (nothing to gate if features don't exist).

**How it is proven:** Screenshot showing Settings pane with new toggles. E2E test: `tests/ui/transcript_settings.e2e` — toggle each setting, verify rows/minimap appear/disappear as expected.

**Height synchronization:** No impact (conditional rendering only).

---

## Shipping Order

**Recommended sequence (by dependency and user impact):**

1. **Chunk 1: Date dividers** — foundational, no blockers, high user impact (multi-day threads are hard to read without dates).
2. **Chunk 2: Thinking disclosure** — moderate impact, depends on data layer having thinking field (verify first).
3. **Chunk 6: Markdown headers** — polish, no dependencies, improves readability of structured messages.
4. **Chunk 5: Syntax highlighting** — polish, high impact for code-heavy threads, self-contained.
5. **Chunk 4: Delivery status rows** — important for send confirmation, depends on data layer support.
6. **Chunk 8: Link auto-detection** — polish, power-user feature, no blockers.
7. **Chunk 3: Tool fold defaults** — important, unblocks the fold-all/expand-all UX pattern.
8. **Chunk 9: Find exclusion for thinking** — small, depends on Chunk 2.
9. **Chunk 7: Streaming animation** — polish, depends on understanding current streaming state API.
10. **Chunk 10: Minimap navigator** — large, requires scroll-sync plumbing, high impact for long threads.
11. **Chunk 11: Settings gates** — final step, gates the features from Chunks 1, 4, 10.

**Ship first 3–5 chunks (8–12 hours) to unlock foundational rendering improvements.**

---

## Known Ceilings & Upgrades

- **Syntax highlighter:** lightweight regex-based version covers 80% of use cases. If performance matters later, move to a proper tree-sitter integration (100+ lines, per-language grammar).
- **Minimap scroll-sync:** current approach re-derives scroll position on every frame. If heavy (many messages), cache scroll geometry per session.
- **Date divider logic:** currently naive 4-hour threshold. Later: user-configurable threshold or "same day" logic based on locale.
