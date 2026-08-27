# Transcript and composer visual audit

Baseline: `main` at `6c7b6e7`, captured at 1100×760 with the deterministic mock clock. The frozen Puffin transcript is `docs/visual-parity/ref/02_thread.png`.

## Ranked defects

1. **The role hierarchy is backwards.** User turns use a neutral charcoal fill while assistant turns use a green-tinted surface. The user turn therefore disappears into the machinery palette and the assistant answer looks like a success alert. Puffin establishes a clear indigo user surface and a quiet blue-black assistant surface.
2. **Tool activity reads as a stack of cards.** Every tool header gets a recessed fill and rounded corners, then expanded output adds another bordered box. A busy thread becomes a dashboard instead of a conversation.
3. **Message actions are visually heavier than the message.** `Copy` and `Retry` appear as wide filled capsules over a 13px transcript. They preserve layout, but the overlay attracts attention before the prose.
4. **Event classes do not share a visual grammar.** Node, skill, status, delivery, spawn, and tool rows use unrelated insets, heights, fills, and icon weight. Transitions between them look accidental even though each row is individually identifiable.
5. **The spawned-agent event is over-boxed.** A full accent border and tinted 46px card gives a child launch more weight than the assistant result it supports.
6. **Expanded tool output is too deep and too bright.** The hard border plus window-dark fill makes output a separate panel. Diff bands then add a third surface layer.
7. **Thinking and delivery disclosures are under-articulated.** They are plain text with a chevron at the column edge, so their affordance is easy to miss and their expanded bodies are not visually tied to the disclosure.
8. **The minimap is a persistent filled rail.** Its background competes with the transcript even when the pointer is nowhere near it. The map should be marks first and a control surface only on hover/drag.
9. **Loading is top-heavy.** A large ring sits in a short 120px block near the pane top while the rest of the transcript is empty. It reads like a local widget rather than a pane state.
10. **The empty transcript is bottom-anchored.** The copy floats just above the composer, leaving an unexplained empty field above it. It should bridge the transcript and composer without looking attached to the input.
11. **Error presentation is a generic top-left note.** It has no title/body hierarchy and does not occupy the same stable state geometry as loading and empty.
12. **Date and outcome dividers compete with turn rhythm.** Full-width rules have almost the same visual weight as container borders, so metadata interrupts reading instead of quietly marking time.
13. **Composer metadata is crowded at narrow widths.** Model, effort, context, status, and tool-fold controls all compete on one 18px line; split panes leave little room for the input’s actual job.
14. **The send control changes proportion while busy.** The 19px circular control becomes a 78×32 wait pill. The input width changes with state even though steering and sending are transient state changes.
15. **Body text is too small for long-form answers.** A 13px face at a 670px assistant measure produces dense lines and weak distinction from 11px machinery metadata.

## Implementation direction

- Establish role surfaces first, then tune spacing against those surfaces.
- Remove filled cards from ordinary machinery rows; reserve bordered surfaces for expanded payloads.
- Give all machinery rows one inset, one metadata scale, and one quiet status vocabulary.
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

Vendor verification was performed against the pinned `vendor/afterhours` source only; no vendor file is modified.
