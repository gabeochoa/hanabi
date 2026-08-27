# Find-in-conversation performance

## Result

The Cmd+F collector no longer normalizes and searches the loaded transcript on every frame. Each pane owns one bounded memo for its active thread. An unchanged frame returns the same ordered match vector without visiting a message.

Measurements use the mock `rbig` transcript at 1180×949, 120 settle frames plus 180 measured frames, `HANABI_PROF=1`, and the query `regression`. Allocation counts are deterministic; CPU time is included only to show scale because this machine is shared.

| loaded messages | `find.collect` before | after | reduction | open/closed allocations before | after | open allocations reduced |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 480 | 3.3937 ms/f | 0.0152 ms/f | 223× | 5.368× | 1.451× | 73.3% |
| 3,672 | 25.6338 ms/f | 0.0971 ms/f | 264× | 28.616× | 1.526× | 94.7% |
| 14,688 | 109.6700 ms/f | 0.4332 ms/f | 253× | 75.137× | 1.527× | 98.0% |

The final open-find allocation counts were 6,660.9, 7,722.6, and 11,398.7 allocations/frame. The corresponding closed counts were 4,590.3, 5,059.4, and 7,466.3. The residual open-find increment is therefore roughly viewport-sized rather than transcript-sized.

## Cache contract

`hanabi::find_memo::Memo` in `src/search/find_memo.h` keeps:

1. normalized paintable lines per message;
2. ordered matches as `(message, logical line, byte offset)` for stepping;
3. the cached byte offsets consumed by highlight painting;
4. row eligibility after applying the operator AST.

The result key includes the pane's thread id and transcript content version, the parsed query text and ordered operator terms, the ASCII case-fold policy, the pane width, long-message fold preference, reasoning visibility, and fold-state revision. Message entries additionally hash the exact source text, role, event kind, subtitle, and tool status.

The memo lives on `ecs::Pane`, not globally and not on the thread. Two panes showing the same thread can therefore hold different queries and widths without evicting or reusing one another's result.

The normalized-message cache is capped at 16,384 entries. The ordered result remains complete because its size is required by the count and stepping contract. Above the normalization cap, the current result is still cached for unchanged frames, while a query or content change recomputes uncached message normalization rather than returning a partial result.

## Invalidation

`Pane::transcriptVersion` changes on every transcript replacement, optimistic append, restored-outbox append, removal, streaming append, and streaming text mutation. On a changed version the memo compares exact per-message content signatures and reuses every unchanged normalized entry. Appending one message and prepending one older message each cause one new normalization in `test_append_and_prepend_are_incremental`; a content edit rebuilds only that message.

Changing the query or operator AST reuses normalized text and rebuilds ordered matches. Width and fold-policy changes invalidate the result key. Event kinds other than `Text`, tool/system roles, and legacy `subtitle == "thinking"` rows never enter the searchable corpus, matching the renderer paths that cannot paint bands.

## Gates and tests

`scripts/find_gate.sh` runs 480- and 3,672-message transcripts with find closed and open. It gates:

1. fewer than 30 transcript rows visited per measured frame after warm-up;
2. at least a 95% whole-result memo hit rate;
3. open/closed allocation ratio below 2×;
4. no more than 16,384 cached normalized messages.

The clean readings are 2.9 and 20.6 rows/frame, 99.33% hits at both sizes, 1.451× and 1.528× allocations, and 240/1,836 entries. Disabling the whole-result hit path produces 797.6 and 6,099.8 rows/frame and the gate fails both arms.

`tests/unit/test_find_memo.cpp` covers unchanged frames, query changes, content mutation, operator AST and tool-state changes, append, prepend, soft-wrap-spanning offsets, fold/width keys, event-kind paintability, two panes on one thread, and the cache bound. The existing scripted find suite continues to hold count, band, soft-wrap, folding, operator, navigation, and window semantics.

## Remaining cost

The collector is no longer the steady-state cost. At 480 messages, `find.paint` is still 0.53 ms/frame and `alloc_sites.sh` attributes 686.5 allocations/frame to `find_highlight::paint_bands`. It must call afterhours' wrapping primitive to reconstruct where byte offsets landed because the renderer does not expose its line layout. The renderer then performs its own plain/styled wrapping again. These are filed as #435–#437 in `afterhours_gaps.md` with the exact vendored mechanisms and rejected alternatives.

The visible rich-body build also remains allocation-heavy: 758.4 allocations/frame at `MainPaneSystem::render_rich_body`, plus 204.5 at its `ComponentConfig` construction site in the same profile. That is viewport-sized and therefore flat across 480, 3,672, and 14,688 messages; #438 records it rather than expanding this change beyond find collection.
