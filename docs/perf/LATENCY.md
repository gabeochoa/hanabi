# Interaction latency

`GATE-0` measures 14 interaction directions from the input event's queue-entry timestamp to the first GPU-rendered ink caused by that interaction.

## Contract

`t0` is captured immediately after the E2E runner queues the synthetic input event and before any input handler runs. It is a monotonic `steady_clock` timestamp. The old mark taken after a command was consumed is gone.

`first ink` is confirmed after `graphics::end_frame()` by reading the headless Metal render target through `metal_capture_render_texture_to_memory`. The probe converts RGBA to luma and compares the committed frame with a baseline captured before the event. Each frame estimates its own background from modal luma, then classifies changed pixels as newly gained ink or removed ink. Appearance, text-appearance, and focus-gain outcomes require at least 40 gained pixels; disappearance, text-disappearance, and focus-loss outcomes require at least 40 lost pixels. The 40-pixel floor is above the composer's source-derived 2x16.8 caret (about 34 pixels): `main_pane_system.h:701,7003` supplies the 21-pixel line height and Afterhours `text_area.h:323,340,350` applies 80% height and 2-pixel width. A caret therefore cannot qualify by itself. A flat frame, failed readback, mismatched shape, missing event timestamp, wrong-direction-only change, missing ink, or missing semantic outcome is a failure rather than a number.

The reading is frozen only on a frame that carries qualifying directional ink and whose declared outcome is reached within the bounded render lag of that same frame. Ink and outcome are associated in time rather than merely both having occurred, so an outcome banked on a frame that painted nothing cannot be settled later by unrelated directional ink arriving after the target is already gone. Key commands account for Afterhours' one-frame synthetic key-delivery boundary without moving `t0`.

`kRenderLagFrames` in `src/util/latency.h` is that bounded render lag, and its value is `0`: directional ink is credited to an outcome only when the outcome is observed within zero presented frames of the ink, which is the same frame. Zero is what this pipeline supports rather than a tolerance to spend. The outcome predicate reads what the render pass produced — the visible-text registry is cleared and refilled by the draw calls, and a widget query requires `was_rendered_to_screen` — and the pixel probe and the predicate are both evaluated after the same `graphics::end_frame()`, so the semantic half cannot lead the pixels. The one frame of lag this instrument does model is on the input side, in `effect_lag_frames`, and allowing it again here would double-count it. Any positive value would re-admit the case the window exists to reject, whose unrelated ink arrives on the very next presented frame. `tests/unit/test_latency.cpp` pins the value and both sides of the window boundary.

Each row reports:

- `event_to_ink_us`: queue entry to GPU-readback confirmation, including the current probe cost.
- `frames`: presented frames from queue entry to the first qualifying ink.
- `changed`, `ink_gained`, and `ink_lost`: full-resolution changed-pixel classifications.
- `required_direction` and `required_ink`: the direction selected by the declared outcome and its qualifying count.
- `ink_before` and `ink_after`: absolute full-frame ink, retained to expose net-removal traps.
- `probe_us`: the readback cost, kept visible instead of hidden in the headline.
- `budget_frames`: the enforced budget.

Only `frames` is budgeted. The microsecond reading is report-only because this shared machine's load moves wall time by multiples; the audit requires deterministic frame/count gates rather than absolute-millisecond gates.

## Measurements

Boulder `KF74T3NW36`, 1100x760, mock backend, pinned clock and UTC fixtures:

| arm | direction / outcome | event-to-ink | gained / lost | required | frames / budget | result |
|---|---|---:|---:|---:|---:|---|
| `click_feedback` | sidebar click → opened transcript | 17,573 us | 1,506 / 0 px | gained 1,506 px | 2 / 4 | pass |
| `focus_gain` | unfocused composer → focused | 16,178 us | 1,542 / 479 px | gained 1,542 px | 2 / 3 | pass |
| `focus_loss` | focused composer → unfocused | 35,040 us | 8,117 / 11,438 px | lost 11,438 px | 2 / 3 | pass |
| `popup_open` | slash input → popup present | 11,804 us | 29,760 / 394 px | gained 29,760 px | 1 / 3 | pass |
| `popup_close` | Escape → popup absent | 23,298 us | 0 / 29,760 px | lost 29,760 px | 2 / 3 | pass |
| `sheet_open` | Rename action → sheet present | 30,725 us | 10,440 / 43,863 px | gained 10,440 px | 2 / 4 | pass |
| `sheet_close` | Escape → sheet absent | 8,537 us | 45,705 / 14,474 px | lost 14,474 px | 2 / 4 | pass |
| `typing_search` | first search key → clear affordance present | 5,009 us | 11,756 / 9,176 px | gained 11,756 px | 1 / 4 | pass |
| `scroll_away` | wheel away from end → jump affordance present | 17,187 us | 26,685 / 26,753 px | gained 26,685 px | 2 / 5 | pass |
| `scroll_back` | reverse wheel → jump affordance absent | 28,537 us | 29,962 / 35,251 px | lost 35,251 px | 2 / 5 | pass |
| `submit` | Enter → first gained ink; acknowledgement required | 32,609 us | 415 / 877 px | gained 415 px | 3 / 4 | pass |
| `attachment_stage` | picker acceptance → chip present | 115,432 us | 4,773 / 2,042 px | gained 4,773 px | 5 / 7 | pass |
| `session_switch_forward` | t2 → t9 | 3,757 us | 6,537 / 0 px | gained 6,537 px | 1 / 4 | pass |
| `session_switch_back` | t9 → t2 | 4,412 us | 6,273 / 6,448 px | gained 6,273 px | 1 / 4 | pass |

The corrected directional gate moves `submit` from the false frame-2 caret removal to frame 3 with 415 gained pixels, and moves `focus_gain` from the false frame-1 30-pixel removal to frame 2 with 1,542 gained pixels.

The fixtures live in `tests/ui/interaction_latency_budgets.e2e`, `interaction_latency_budgets_two.e2e`, and `interaction_latency_scroll.e2e`.

## Controls

`scripts/latency_delay_sweep.sh` owns the exact 14-label vocabulary and cardinality. It runs all arms normally and with `HANABI_LATENCY_APP_DELAY_FRAMES=7`. The delay holds the queued input before the production input handlers; it does not delay observation. Every arm must move by exactly seven presented frames.

The same gate proves these failure paths return nonzero and carry the expected diagnostic:

1. A real submit arm with budget zero fails over budget.
2. Changed pixels without the declared outcome fail as no outcome.
3. A no-op input with neither ink nor outcome fails as no first ink.
4. A positive arm aimed at an already-present target is refused.
5. Reusing a label is refused as a duplicate arm.
6. Missing, duplicate, and extra measurement rows are each rejected by the exact-set check.
7. The normal shipping binary contains none of the latency command, delay, report strings, or `hanabi::latency` symbols.

`tests/unit/test_latency.cpp` covers luma conversion, per-frame background estimates, a source-derived 2x17 caret, the 40-pixel floor, gained/lost classification, all six outcome-to-direction mappings, wrong-direction rejection for every outcome, shape mismatch, queue timestamp arithmetic, delivery ordering, duplicate labels, preexisting targets, no ink, no outcome, and unreadable frames. Its paired protocol proves caret removal cannot settle any appearance/focus-gain arm and caret appearance cannot settle any disappearance/focus-loss arm.

## Threading and shipping

The event stamp, delay, ECS/input handling, layout, rendering, pixel readback, outcome check, and result adoption all run on the frame thread. No worker reads or writes Afterhours state; the pinned Afterhours contract only guarantees that Sokol callbacks share one thread and provides no shared-ECS safety contract. The suite's process timeout is the out-of-process watchdog for a wedged frame thread.

The implementation is included only below `AFTER_HOURS_ENABLE_E2E_TESTING`. The shipping build has no watcher, event hook, pixel readback, delay branch, environment lookup, or reporting path.
