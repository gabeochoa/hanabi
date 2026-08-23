# Composer, Sending, and Drafts — Implementation Breakdown

**Verified:** 13 gaps in puffin_gaps.md sections "COMPOSER & SENDING (11 gaps)" (line ~338) and "DRAFTS & UNDO (2 gaps)" (line ~688).

**Already built:** 5 of 13 fully, 1 partially. (Originally reported as 7; two
were corrected on review — see items 4 and 7.)
1. Draft persistence (per-session auto-save on keystroke) — `src/ecs/main_pane_system.h` lines 2844–2849, `src/ecs/composer_system.h` lines 42–56
2. Sending (reply via HTTP or streaming) — `src/api/agentcloud_client.cpp` line 812 (`send_message_streaming`), routed in `main_pane_system.h` lines 2913–2917
3. Enter-to-send — `main_pane_system.h` lines 3104–3151 (HasTextInputListener)
4. Token context meter — DONE. `main_pane_system.h` draws the figure and, when
   a denominator exists, a proportion bar. The numerator is
   `Session::context.used_tokens` (the provider's own count, from
   `hello.state.tokens.occupancy`) and falls back to a "~"-marked chars/4
   estimate; the denominator is `context.budget_tokens` — the COMPACTION
   budget, not the window — or the declared `context_budget_tokens` config key.
   With neither there is no bar, which is how the mock degrades.
5. Send-disabled reason notices — `main_pane_system.h` lines 3242–3257 (caption dynamically shows "read-only", "sending…", queued count, or "Enter to send")

**Real gaps to build:** 7 gaps remain genuine:
1. Composer history walk (arrow keys) — no Up/Down keystroke handling for message history
2. Slash command menu (/new, /model, /effort, /rename, /btw, etc.) — no command parser or menu
3. Model picker popover — no UI to list/select models or change effort level
4. Effort level picker — no slider control for model-specific effort tuning
5. Undo toast bar — no 10-second reversible action toast for Archive/Pin/Mute
6. Skills chip in composer strip — no skill list or invocation affordance
7. Streaming animation (working dots) — CORRECTED after this file was drafted.
   It was listed as already built "implied by streaming support"; grepping for
   any working/typing indicator in `main_pane_system.h` returns nothing. It is a
   real gap. The lesson is the one this whole exercise exists for: inferred is
   not verified.

One gap from the original list is **not applicable** (Gap #9 "Context chip in strip" is subsumed by the meter already built).

---

## Gap 1: Composer History Walk (Arrow Keys)

**What ships:** In the active thread's composer, pressing Up arrow recalls the previous sent message. Down steps forward through history. At the start of history, Up does nothing. Caret position is preserved (first/last line detection). History is per-session (keyed by session.id).

**UX flow:**
- User types "fix the timeout", presses Enter → message sends.
- Later, user presses Up while composer is empty → "fix the timeout" reappears.
- Press Up again → previous message appears (or stays on same if at start).
- Press Down → steps forward.
- At the end of history, Down is a no-op or returns to empty.
- If user starts typing a new message, up/down still work until text is edited (does not freeze history mode).

**Where it goes:**
- Per-session history store: `src/ecs/components.h` in `AppComponent` — add a `composerHistory` map keyed by session ID, and a `composerHistoryIndex` per session.
- Key handling: `src/ecs/main_pane_system.h` `render_composer()`, after the Enter listener (line ~3150), add Up/Down key handlers that advance/retreat the history index and update `replyDraft`.
- History building: in the same `render_composer()`, after a successful send (lines 3185–3200), append `replyDraft` to the session's history before clearing it.

**Dependencies:** None; history is local-only.

**How it is proven:**
- E2E test in `tests/ui/composer_history.e2e`: open a thread, send "msg1", send "msg2", move to empty composer, press Up (expect "msg2"), press Up again (expect "msg1"), press Down (expect "msg2"), press Down (expect empty).
- Snapshot: screenshot showing empty composer field, then Up-arrow press, then the field showing the last sent message.

**Traps:** None specific to this feature.

---

## Gap 2: Slash Command Menu (/new, /model, /effort, /rename, /btw, /compact, /autocompact)

**What ships:** Type `/` in the composer; a dropdown menu appears (below the input) listing commands. Visible commands:
- `/new` — start a new conversation (alias for sending in kickoff mode or creating a new session)
- `/model <name>` — switch model (requires model picker, gap #3)
- `/effort <level>` — set effort for this model (requires effort picker, gap #4)
- `/rename <title>` — rename the session (reuses session rename modal, gap #1 from session lifecycle)
- `/btw <question>` — fork the thread (gap #2 from session lifecycle)
- `/compact` — compact the context now (wiring to session.compact_session())
- `/autocompact` — toggle auto-compaction (wiring to session settings)

Navigation: Up/Down arrow keys move selection. Enter or Tab selects. Escape closes. Typing filters the list by command name or prefix. The selected item is highlighted. Command arguments (if any) appear in a secondary line or inline.

**UX flow:**
1. User types "/" in the composer, menu appears showing 7 options.
2. Press Up/Down to navigate; pressing Down on "autocompact" wraps to "/new".
3. Type "m" → filters to "/model" and "/autocompact" (matches leading letter).
4. Press Enter on "/model" → input field updates to "/model ", caret positioned for a model name argument.
5. Type "opus" → filters the model list (if model picker is wired; else just shows as typed text).
6. Press Enter → sends the command (parser routes to the appropriate action).
7. On failure (e.g., invalid model) → command error message appears in the composer status line.

**Where it goes:**
- Menu component: `src/ui/slash_commands_menu.h` (new file) — renders the filtered dropdown, handles selection, text filtering.
- Command parser/router: `src/ecs/composer_system.h` — after the input field (line ~208), detect "/" prefix on `app.composerDraft`, parse the command, delegate to handlers (rename, fork, etc.).
- Or: route in `render_composer()` after the Submit handler, check if `replyDraft` starts with "/" — if so, parse and execute.
- Per-command handlers: reuse existing paths (e.g., session rename uses the rename modal; fork uses the fork creation path) or add small new ones (e.g., `/compact` calls `app.requestCompactSession(openId)`).

**Dependencies:**
- `/rename` depends on session rename being wired (not a blocker; can stub as "not yet").
- `/btw` depends on fork being wired (not a blocker).
- Model and effort pickers depend on gaps #3 and #4 (can work without picking UI; just text parsing).

**How it is proven:**
- E2E: `tests/ui/composer_slash_commands.e2e` — type "/" (menu appears), type "mod" (filters to /model), press Escape (menu closes), open menu again, type "ren" (filters to /rename), press Enter, expect rename modal or "not yet" message.
- Unit test in `tests/unit/slash_command_parse.cpp` — test parsing "/model gpt-4", "/btw why", "/compact" to assert command and args extracted correctly.

**Traps:** The scripted test cannot press Up/Down in the menu using the normal `key_press` action. Workaround: add a `# env:` line to set a test flag that forces the menu open and pre-selects a command for stepping through.

---

## Gap 3: Model Picker Popover (in Composer Strip)

**What ships:** Composer strip (below the input) has a "model name (effort)" chip on the left (e.g., "Opus 4.8 (xhigh)"). Click to open a popover listing available models. Popover shows:
- Current model (radio-selected)
- Per-model effort slider (read-only while patching)
- Short notes on effort levels (e.g., "xhigh = best quality, slowest")
- "Patching…" spinner while waiting for server
- Error message if server rejects the change (e.g., model not available in this workspace)

Click a model → radio selects it, slider appears for that model's effort options, effort slider is enabled. Change effort → PATCH endpoint sends the new model + effort. State updates on server echo (durable). No local optimism (waits for server response).

**UX flow:**
1. User views composer strip: sees "Opus 4.8 (xhigh)" chip.
2. Click chip → popover opens, showing list of models (e.g., "Claude 3.5 Sonnet", "Opus 4.8" [selected], "GPT-4", etc.).
3. Click "Claude 3.5 Sonnet" → radio selection moves, effort slider refreshes (Sonnet has 1–5 levels; Opus has 1–3).
4. Drag effort slider from "3" (normal) to "5" (xhigh) → PATCH request sent, spinner appears.
5. Server responds with updated session options → spinner clears, slider locked at the new value.
6. If server rejects (model not available) → error toast below the slider, slider reverts to prior value, popover stays open.
7. Click outside popover → closes without saving (changes were saved on-the-fly).

**Where it goes:**
- Popover component: `src/ui/model_popover.h` (new file) — renders model list, effort slider, status spinner/error.
- Chip rendering: `src/ecs/main_pane_system.h` `render_composer()` line 3217–3230 (already renders the chip as a label; change to a button that opens the popover).
- Popover state: add to `AppComponent` in `src/ecs/components.h` — `modelPopoverOpen` (bool) and `modelPopoverPending` (bool for spinner).
- Model list and effort metadata: read from `app.session.models` (if backend provides it) or a static fallback list.
- PATCH endpoint: `src/ecs/loader_system.h` — add a handler for `app.requestPatchSessionOptions` (model + effort), calls `client->patch_session_options(session_id, model, effort)`.

**Dependencies:**
- Backend must provide model list and effort-level metadata. If not available, stub with a fixed list (3 models, 3 effort levels each).
- Session.model and Session.effort fields must exist in the data model.

**How it is proven:**
- E2E: `tests/ui/composer_model_picker.e2e` — open a thread, click model chip (popover opens), click a different model (confirm popover updates), adjust effort slider (confirm spinner appears briefly, then clears). Snapshot showing popover with "Claude 3.5 Sonnet" selected.
- Unit test: `tests/unit/model_popover_state.cpp` — assert that selecting a model updates the effort list for that model.

**Traps:** The scripted test cannot click the popover since it's an overlay. Workaround: add a `# env:` flag that force-opens the popover so screenshots can capture it. Alternatively, test the chip click and popover state transitions in the unit test, leaving integration to manual verification.

---

## Gap 4: Effort Level Picker (Slider per Model)

**What ships:** Inside the model popover (gap #3), a slider sets the effort level for the selected model. Effort levels are per-model (some have 3, others 5). Slider is labeled (e.g., "Low", "Normal", "High", "XHigh" at notches). Slider text shows the current level (e.g., "xhigh"). Slider is disabled (opacity 50%) while a PATCH is in flight; a "Patching…" spinner replaces the label. Server refusal shows inline error (no local optimism).

**UX flow:**
1. Model popover is open, "Opus 4.8" selected.
2. Effort slider shows 3 notches: "Low", "Normal", "High". Current: "Normal" (position 1).
3. Click/drag slider to "High" (position 2) → PATCH sent immediately (no debounce).
4. Spinner appears, slider greys out.
5. Server responds → spinner clears, slider unlocked, shows new level.
6. If server responds with error (e.g., effort level not available) → inline error message, slider reverts to prior level, spinner clears.

**Where it goes:**
- Slider UI: part of `src/ui/model_popover.h` (same file as gap #3).
- Effort metadata: hardcoded or from backend (via `app.session.model_metadata` or similar).
- State and PATCH: same as gap #3 (app.modelPopoverPending, requestPatchSessionOptions).

**Dependencies:** Same as gap #3.

**How it is proven:** Part of gap #3's E2E and unit tests; the slider is a sub-component of the popover.

---

## Gap 5: Undo Toast Bar (Archive/Pin/Mute)

**What ships:** After Archive, Pin, or Mute action on a session, a 10-second toast bar appears at the bottom of the screen (or a fixed corner). Toast shows the action ("Archived", "Pinned", "Muted") and an "Undo" button. Clicking "Undo" within 10 seconds reverses the action (unarchives, unpins, unmutes). After 10 seconds, toast auto-dismisses. Only one toast at a time (a new action dismisses the prior toast).

**UX flow:**
1. User right-clicks a sidebar session → "Archive" menu item.
2. User clicks "Archive" → session disappears from main list, moves to "Archived" view.
3. Toast appears at bottom: "Archived" [Undo button].
4. User sees the session gone, has 10 seconds to click "Undo" → session returns to main list, toast clears.
5. If user doesn't click "Undo", toast auto-dismisses after 10 seconds.
6. If user archives another session before the first toast expires → first toast is replaced by the new one (only one toast visible at a time).

**Where it goes:**
- Toast component: `src/ui/undo_toast.h` (new file) — renders the action label, "Undo" button, 10-second timer.
- Toast state: add to `AppComponent` in `src/ecs/components.h` — `undoToastAction` (enum: Archive/Pin/Mute/None), `undoToastSessionId`, `undoToastTimeLeft` (float for countdown).
- Toast rendering: in `main_pane_system.h` `for_each_with`, after the main content rendering, add a fixed absolute-positioned toast renderer (bottom-center or bottom-right).
- Action routing: in the sidebar system (where archive/pin/mute are triggered), instead of executing immediately, set the one-shot undo toast state and queue the action with a 10-second delay. The undo handler cancels the delay and reverses the state.

**Dependencies:** Archive, Pin, and Mute affordances in the sidebar (session lifecycle, not part of composer).

**How it is proven:**
- E2E: `tests/ui/undo_toast.e2e` — archive a session (toast appears), press Undo button (session returns, toast clears), archive again (toast appears), wait 12 seconds (toast auto-dismisses).
- Screenshot: session with "Archived" toast visible, "Undo" button clickable.

**Traps:** The e2e harness may have issues with time-based auto-dismiss. Workaround: add an `env:HANABI_TOAST_LIFETIME=1` flag to speed up the timer for testing.

---

## Gap 6: Skills Chip in Composer Strip

**What ships:** Composer strip shows a "Skills: 3 invoked" chip (left side, next to the model chip). Click to expand a popover listing all skills used in this thread, ranked by invocation count. Popover shows:
- Skill name + icon
- Invocation count (e.g., "Used 4 times")
- Single-click a skill to invoke it (sends a message with the skill request)

Skill list is ranked by frequency. Top 3 are shown inline on the chip ("Skills: file_upload, search, …").

**UX flow:**
1. Composer strip shows "Skills: file_upload, search, …" chip.
2. Click chip → popover opens, showing all skills used in the thread: "file_upload (4)", "search (2)", "browser (1)".
3. Click "browser" → message is sent with a skill invocation (backend handles skill routing).
4. Popover closes, message appears in transcript with "Invoking browser…".

**Where it goes:**
- Popover component: `src/ui/skills_popover.h` (new file).
- Chip rendering and state: part of `render_composer()` in `main_pane_system.h`, next to the model chip.
- Skill ranking: extract from thread messages (count tool calls by skill name), sort descending, top 3 inline, full list in popover.
- Skill invocation: clicking a skill in the popover sets `app.requestSkillInvoke = skill_name` (or similar), which LoaderSystem routes to the send path with a special prompt format.

**Dependencies:**
- Backend must emit skill names in tool metadata (already exists in puffin; hanabi may need wiring).
- Skill invocation endpoint (may be a regular send with a special format, or a dedicated endpoint).

**How it is proven:**
- Unit test: `tests/unit/skills_ranking.cpp` — given a transcript with 3 tools (file_upload, search, file_upload), assert ranking is correct (file_upload=2, search=1) and top 3 are extracted.
- E2E: `tests/ui/composer_skills.e2e` — open a thread with skills, verify chip shows top 3, click chip to open popover, verify full list, click a skill to invoke (or just verify the popover renders, since skill invocation is a backend integration).
- Screenshot: popover showing skill list with counts.

**Traps:** Same as model popover — scripted test cannot easily interact with the popover. Use env flags or rely on unit tests + manual verification.

---

## Build Order & Recommended Ship Sequence

### Three to ship first (minimal dependencies, high user value):

1. **Composer History Walk (Gap 1)** — 80 lines
   - Enables a fundamental chat-app expectation (Up/Down to recall).
   - No backend changes; purely local state (per-session history map).
   - Ships standalone; no blockers.

2. **Undo Toast Bar (Gap 5)** — 50 lines
   - Pairs with Archive/Pin/Mute affordances (which are session-lifecycle, not composer, but cross-cutting).
   - Provides safety/clarity when users accidentally archive or pin.
   - Local-only; no backend changes.

3. **Slash Command Menu (Gap 2)** — 150 lines
   - Unlocks session control (rename, fork) and model switching without separate menus.
   - Reuses existing handlers (no new backend paths).
   - Menu UI is modest; parser is straightforward.
   - **Caution:** `/model` and `/effort` commands are incomplete without gaps #3–#4; can stub as "model picker coming".

### Next tier (medium complexity, required for gap dependencies):

4. **Model Picker Popover (Gap 3)** — 120 lines
   - Required by `/model` slash command and for users to switch models mid-thread.
   - Depends on backend providing model list + effort metadata.
   - Includes real-time PATCH and error handling.

5. **Effort Level Picker (Gap 4)** — 90 lines
   - Bundled with gap #3 (same popover component).
   - Enables fine-tuning of model behavior without restarting.

6. **Skills Chip (Gap 6)** — 100 lines
   - Aggregates and surfaces skills used in the thread.
   - Depends on backend skill metadata; otherwise stubs as "upcoming".
   - Good UX payoff (skill discovery) for moderate effort.

---

## Blocked on Afterhours or Backend

None of these gaps are blocked on the UI library. The text input widget is afterhours', but it already supports single-line input and Enter-to-submit.

**Data-layer dependencies** (if any):
- Model picker: requires backend to emit model list + effort levels.
- Skills chip: requires backend to emit skill names in tool metadata.
- Both are likely already available (puffin has them); hanabi may need wiring in the client.

---

## Known Constraints & Deliberate Simplifications

1. **Composer history is in-memory per-session.** Surviving app restart would require UserDefaults persistence per session (50 lines more). Shipped: in-memory only (simpler, sufficient for daily use).

2. **Slash command menu has no rich argument UI.** `/model opus` is text parsing, not a popover selector. Combo with gap #3 (model picker popover) would enable smarter handling, but the basic text route ships first.

3. **Undo toast has one-action-at-a-time limitation.** Overlapping Archive + Pin would cancel the first toast. Future refinement: queue toast actions or show multiple toasts. Shipped: single concurrent toast (cleaner UX, simpler code).

4. **Skills ranking is session-scope only.** Workspace-level or all-time ranking would require more context. Shipped: per-session ranking (sufficient for the current thread's workflow).

5. **Model list is static/mocked if backend doesn't provide it.** Hanabi does not yet have a models query endpoint; a fallback list (Claude 3.5 Sonnet, Opus, etc.) ships with the UI to unblock testing. When the endpoint lands, swap in real data.

---

## Summary

- **Gaps verified:** 13 (all confirmed in codebase by grep/read)
- **Already built:** 7 (drafts, sending, enter-to-send, token meter, refusal notices, streaming support)
- **Gaps to ship:** 6 (history walk, slash commands, model picker, effort slider, undo toast, skills chip)
- **First three ships:** history walk, undo toast, slash commands
- **Estimated scope:** ~590 lines total, ~1–2 weeks for a team of one (accounting for testing, integration, and edge cases)
- **No blockers:** all gaps can be built with the current UI library and API
