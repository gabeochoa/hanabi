# Transcript and composer visual audit

Baseline: `main` at `6c7b6e7`, captured at 1100×760 with the deterministic mock clock. The frozen Puffin transcript is `docs/visual-parity/ref/02_thread.png`.

## Ranked defects

1. **Tool activity reads as a stack of cards.** Every tool header gets a recessed fill and rounded corners, then expanded output adds another bordered box. A busy thread becomes a dashboard instead of a conversation.
2. **Message actions are visually heavier than the message.** `Copy` and `Retry` appear as wide filled capsules over a 13px transcript. They preserve layout, but the overlay attracts attention before the prose.
3. **Event classes do not share a visual grammar.** Node, skill, status, delivery, spawn, and tool rows use unrelated insets, heights, fills, and icon weight. Transitions between them look accidental even though each row is individually identifiable.
4. **The spawned-agent event is over-boxed.** A full accent border and tinted 46px card gives a child launch more weight than the assistant result it supports.
5. **Expanded tool output is too deep and too bright.** The hard border plus window-dark fill makes output a separate panel. Diff bands then add a third surface layer.
6. **Thinking and delivery disclosures are under-articulated.** They are plain text with a chevron at the column edge, so their affordance is easy to miss and their expanded bodies are not visually tied to the disclosure.
7. **The minimap is a persistent filled rail.** Its background competes with the transcript even when the pointer is nowhere near it. The map should be marks first and a control surface only on hover/drag.
8. **Loading is top-heavy.** A large ring sits in a short 120px block near the pane top while the rest of the transcript is empty. It reads like a local widget rather than a pane state.
9. **The empty transcript is bottom-anchored.** The copy floats just above the composer, leaving an unexplained empty field above it. It should occupy a stable centered state.
10. **Error presentation is a generic top-left note.** It has no title/body hierarchy and does not occupy the same stable state geometry as loading and empty.
11. **Date and outcome dividers compete with turn rhythm.** Full-width rules have almost the same visual weight as container borders, so metadata interrupts reading instead of quietly marking time.
12. **Composer metadata is crowded at narrow widths.** Model, effort, context, status, and tool-fold controls all compete on one 18px line; split panes leave little room for the input’s actual job.
13. **The send control changes proportion while busy.** The 19px circular control becomes a 78×32 wait pill. The input width changes with state even though steering and sending are transient state changes.
14. **Typography is easy to worsen by eye.** A 14px/18px trial looked more spacious in isolation but moved the measured Puffin main-region structural difference from 2.20% to 3.06% and broke proven turn geometry. The existing 13px/16px pair is the correct baseline for this renderer.

## Implementation direction

- Preserve the measured user/assistant surfaces, reading width, and type rhythm.
- Remove filled cards from ordinary machinery rows; reserve bordered surfaces for expanded payloads.
- Give all machinery rows one inset and one quiet metadata scale.
- Keep hover actions conditionally constructed and absolutely overlaid so rest-state cost and geometry remain unchanged.
- Keep every render decision local to one item and preserve the existing width-keyed transcript cache.
- Keep the minimap on the item list already built by the transcript; do not add another message pass.
- Keep the composer width stable across send, steer, and busy states.

## Rejected approaches and constraints

- Adding more pills would separate labels but make the hierarchy noisier. **Hanabi reference.** Gap index family `#458/#459` records that visible text supplies missing semantics while conditional construction keeps hidden actions at zero entities; the vendored `imm::div` implementation constructs immediately, so omitted calls are the only zero-cost hidden state.
- Building a second event model for styling would duplicate the transcript item walk. **Hanabi reference.** Gap index row `#455`, family `#326/#224`, records the existing variable-height scan and its long-thread CPU cost; styling stays in each item renderer and consumes the existing `Item` list.
- Making the minimap a second per-message overlay would add O(messages) visual work. **Hanabi reference.** Gap index row `#455`, family `#327`, and `MainPaneSystem::minimap_rail` show why marks are grouped from the existing item vector.
- Keeping invisible hover widgets would preserve tab stops and allocations. **Hanabi reference.** Gap index row `#459`, family `#458`, is the verified negative result: `imm::div` calls `init_component` immediately, while `mouse_was_in_subtree` only needs a hit-testable host.
- Using percent width for an absolute hover-action overlay would trip the layout validator. **Hanabi reference.** Gap index family `#97/#275`; vendored `autolayout.h` rejects `absolute && Dim::Percent`, so the existing pixel width remains the correct workaround.
- Enlarging message type independently of the reference would weaken alignment without fixing hierarchy. **Hanabi reference.** Gap index family `#340/#435/#436` covers the visible rich-text path; the measured trial was reverted, leaving the width-keyed cache and known 13px/16px geometry intact.
- Hiding narrow-pane controls with invisible widgets would retain focus targets and per-frame work. **Hanabi reference.** Gap index row `#459`, family `#458`; the compact composer omits those widgets entirely and leaves the input, status, tool-fold control, and send action available.

Vendor verification was performed against the pinned `vendor/afterhours` source only; no vendor file is modified.

## Review evidence

Each row has a committed `docs/screenshots/review/transcript-<state>-before.png` and `-after.png` pair.

| state | pixels changed | reason |
|---|---:|---|
| normal transcript | 0.00% | control capture; measured message geometry retained |
| assistant hover actions | 0.16% | narrower action overlay on the message surface |
| user hover actions | 0.32% | stable compact copy/retry treatment |
| empty transcript | 0.34% | centered state geometry |
| long transcript / minimap | 5.02% | no persistent minimap trough |
| tools expanded | 0.00% | control capture; fixture has no visible tool row |
| tool details expanded | 12.81% | flat headers and inset code surfaces |
| split view | 0.07% | stable circular in-flight action |
| loading | 0.13% | centered pane state |
| thinking closed | 0.29% | aligned disclosure and stable send state |
| streaming | <0.01% | stable circular in-flight action |
| busy event thread | 4.51% | one machinery inset and quieter spawn/tool hierarchy |
| failed tool | 4.62% | flatter failed row and code surface |
| multiline draft | 0.00% | control capture; multiline growth remains stable |
| 760px split | 6.11% | compact metadata and bounded pane controls |
| thinking open | 0.85% | disclosure/body alignment |

The final frozen-Puffin comparison remains **3.90% structural on shared surfaces** and **2.20% in the main region**, exactly matching the starting point. The discarded type experiment measured **4.50%** and **3.06%** respectively, so it was not retained.

## Performance verification

- Transcript slope: 3,769.0 → 3,837.4 allocations/frame from 60 → 480 messages, or **0.163 allocations/message** against a limit of 12. The 480-message render cache remained **85.5%** hit after rebasing onto the incremental transcript index. **Hanabi reference.** Gap index row `#455`, family `#326/#224`; `scripts/perf_transcript_slope.sh`.
- Text measurement: 6.79 → 13.39 measures/frame, or **0.0157/message** against a limit of 0.05. The line-count cache remained **96.32%** hit and the advance cache **97.69%** hit at 480 messages. **Hanabi reference.** Gap index family `#340/#435/#436`; `scripts/perf_text_gate.sh`.
- Allocation levels: **2,739/frame** for 480-message transcript and **1,025/frame** for a six-line draft, below ceilings of 3,300 and 1,250. **Hanabi reference.** Gap index rows `#305/#455`; `scripts/alloc_gate.sh`.
- Busy-event scaling: 333 → 505 widgets and 2,375 → 2,355 allocations/frame from 15 → 240 turns; widget slope **0.76/turn** and allocation slope **−0.09/turn**. Minimap marks stayed at 241 under the 400 ceiling. **Hanabi reference.** Gap index rows `#455/#459`, family `#327`; `scripts/events_gate.sh`.
- Visual determinism: **40/40** baselines reproduced at a 0.0% threshold; the fast screenshot subset also passed 8/8 inside `make test`.
