# hanabi — feedback from Gabe, 2026-08-26

Reconciled against current `main` (`e249747`) on 2026-08-27. The live list had
not been updated as its branches landed. The status below is code- and
test-backed; there is no merge queue left in this file.

Status key: `DONE` · `OPEN`

## A. Session event fidelity

### A1. His messages to me are visible — DONE (`201b663`, `2ae06eb`)

Agentcloud transcript folding now keeps the authored reply separate from
reasoning and tool JSON, and the event model no longer assumes every frame is a
four-role chat message. Covered by `tests/ui/every_event_class_has_a_row.e2e`.

### A2. Thinking is visible — DONE (`2ecc99b`, `c56135b`)

Thinking renders as its own event row and disclosure with the hidden-reasoning
size stated. Covered by `tests/ui/thinking_disclosure.e2e` and
`tests/ui/thinking_indicator_sits_on_the_text_column.e2e`.

### A3. Deliveries are visible — DONE (`2ecc99b`)

Delivery frames render as their own event class instead of being flattened into
a shared grey line. Covered by `tests/ui/every_event_class_has_a_row.e2e`.

### A4. Subagents are visible — DONE (`2ecc99b`, `c56135b`)

Subagent events and the transcript rollup render independently, including
running/done state. Covered by `tests/ui/subagent_toggle.e2e` and
`tests/ui/subagent_chips_stay_inside_the_rollup.e2e`.

### A5. Nodes are visible — DONE (`2ecc99b`)

Node events have a dedicated row in the event model. Covered by
`tests/ui/every_event_class_has_a_row.e2e`.

### A6. Skills are visible — DONE (`2ecc99b`)

Skill events have a dedicated row in the event model. Covered by
`tests/ui/every_event_class_has_a_row.e2e`.

## B. Input and keyboard

### B1. Shift+Enter inserts a newline — DONE (`e2d4bc8`)

The composer is a multiline `text_area`; Enter still sends and Shift+Enter
breaks the line. Covered by `tests/ui/composer_shift_enter.e2e`.

### B2. Option+Backspace deletes a word — DONE (`101e215`)

The macOS editing chord table now includes word motion and word deletion.
Covered by `tests/ui/composer_word_editing.e2e`.

### B3. Cmd+A selects all text — DONE (`101e215`, `a3a9d98`)

Select-all and selection-extending motion use the same editing path in the
composer. Covered by `tests/ui/composer_shift_selection.e2e`.

## C. Visual and layout

### C1. Focus rings — DONE (`eed2f62`, `f9f4989`, `2410dd7`, `5163857`)

The ring is a single backdrop-aware band, appears for keyboard navigation,
does not arm on text-field arrow keys, and follows each widget's geometry.
Covered by `tests/ui/focus_ring_waits_for_the_keyboard.e2e` and
`tests/ui/an_arrow_key_is_not_a_focus_ring.e2e`.

### C2. Buttons outside their bounds — DONE (`060ee5a`, `563ebbe`, `759f644`, `12472e5`)

The containment audit identified the real escapes, shared row geometry removed
the repeated segment overflow, and the send/subagent rows were corrected.
`scripts/bounds_gate.sh` now fails on any new parent-bound escape.

### C3. Typing indicator alignment — DONE (`fb71e0d`)

The indicator is anchored to the message text column rather than the bubble
padding. Covered by `tests/ui/thinking_indicator_sits_on_the_text_column.e2e`.

### C4. Pinned threads in the sidebar — DONE (`df19be0`)

Pinned sessions form the sidebar prefix before ordinary sessions. Covered by
`tests/ui/pinned_threads_head_the_sidebar.e2e`.

### C5. Steer is an icon — DONE (`e3bf42c`)

The idle action is a bent-stem mark with an accessible name instead of a large
button. Covered by `tests/ui/steer_is_an_icon_with_a_name.e2e`.

### C6. Dragging the minimap — DONE (`8004943`, merge `3f1a2ca`)

Press-drag-release continuously scrubs the transcript and preserves the pane's
independent scroll state. Covered by `tests/ui/minimap_drag.e2e` and
`tests/unit/test_minimap_scrub.cpp`.

### C7. Mouse wheel scrolling — DONE (`3316179`, `5ae33ee`, merge `cb47404`)

A wheel gesture breaks the bottom-follow latch, scrolling back to the end
re-arms it, and split panes remain independent. Covered by
`tests/unit/test_follow_latch.cpp`, `tests/ui/wheel_scrolls_the_transcript.e2e`,
and `tests/ui/wheel_scrolls_the_pane_under_the_pointer.e2e`.

The remaining hardware distinction is not this ask: afterhours exposes trackpad
and wheel as the same float, recorded as gap #405. `HANABI_SCROLL_SPEED` remains
the application workaround.

## D. Audits

### D1. Explain and correct search — DONE (`7d1920f`, merge `67f0696`)

`docs/SEARCH.md` documents the three searches. The follow-up made deep search
incremental and honest about coverage/truncation, and made find count the whole
thread rather than only the current render window.

### D2. Verify commit descriptions against diffs — DONE (`116ec8a`, `38e7e69`, merge `ffc5ae7`)

`docs/COMMIT_AUDIT.md` records the full audit; 58 copied claims and numbers were
corrected in docs and source comments.

## E. Features

### E1. Pane splitting — DONE (`a2f7e04`, `1a70900`, merge `274f369`)

Two panes now own independent thread, scroll, draft, focus, find, minimap and
follow-latch state, with a draggable divider. Covered by
`tests/ui/pane_split.e2e` and `tests/unit/test_pane_memory.cpp`.
