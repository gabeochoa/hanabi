# Interaction latency

Nine interaction paths, measured in frames from the frame an input is consumed
to the frame the ink it produced first appears.

## Why frames, not milliseconds

This box is shared. `scripts/alloc_gate.sh` records frame time swinging
1.6–6.5 ms under load 10–34 while the allocation counts beside it stay stable,
and `scripts/retire_gate.sh` says it outright: no milliseconds anywhere. A
frame count is the same number on an idle laptop and a loaded one; a
millisecond budget on this hardware is a coin flip. Every number below is
frames.

## The measurements

Measured on the committed bytes. Budget is measured + headroom, and the
headroom is deliberately small — a budget nothing can trip is decoration.

| arm | from → to | frames | budget |
|---|---|---:|---:|
| `focus` | click → the composer is the focused element | 1 | 5 |
| `popup_open` | `/` → the slash menu over a live thread | 1 | 5 |
| `typing_search` | typing in the sidebar → the clear affordance appears | 2 | 8 |
| `sheet_open` | menu item → the rename modal is built | 2 | 6 |
| `scroll` | wheel → `jump_to_bottom` appears | 4 | 8 |
| `attachment` | picker choice → the first attachment chip is drawn | 5 | 9 |
| `submit` | Enter → the assistant's acknowledgement reaches the transcript | 6 | 12 |
| `click_feedback` | sidebar row click → a line only that thread contains | 7 | 11 |
| `session_switch` | click a second tab → content only that thread has | 7 | 11 |

Scripts: `tests/ui/interaction_latency_budgets.e2e` (typing/search, focus,
submit, click feedback), `..._budgets_two.e2e` (session switch, popup,
attachment, sheet), `..._scroll.e2e` (scroll). Three scripts because each
needs its own fixture and the nine together exceed the runner's per-script
budget.

## How an arm is built

Two commands, and the first one is the load-bearing half:

```
watch_ink <label> <ui|text|focus> <target>   # arm; the target must be ABSENT
<the input>
wait_frames N
expect_latency <label> <max_frames>
```

`watch_ink` **refuses to arm on a target that is already on screen**, and that
refusal is the point. The first version of this instrument watched components
that already existed — `transcript_scroll`, `tab_strip`, the composer input —
so it was reporting the runner's own pacing between two commands and calling it
interaction latency. Finding a component that was already there cannot say the
interaction produced it. Now such an arm cannot be written: the script fails
with `'<name>' is ALREADY present — this arm would measure the runner's pacing,
not the interaction`.

Three kinds of target, because interactions produce three kinds of evidence:

- `text` — a string drawn this frame, read from afterhours'
  `VisibleTextRegistry`, the same oracle its own `expect_text` uses.
- `ui` — a laid-out component that did not exist before the input.
- `focus` — an exact state transition, for focus, which produces no new ink of
  its own.

The clock starts when an input is **consumed**, not dispatched.
`LatencyInputStampSystem` runs after the builtin handlers and stamps every
pending watcher on the frame the input took effect, so the gap between arming
and acting is not counted. An early version stamped at dispatch and reported
`typing 53` for a 49-character line — it was measuring the type command's own
duration.

Settling is a **system**, not a command. `LatencyObserverSystem` runs after
`build_systems` and checks every armed watcher once per frame, so the frame it
records is the frame the ink actually appeared on. It cannot be a command: the
runner fails a command that does not consume on its first dispatch
(`HandleUnknownCommand`), so a script-side assertion looks exactly once and
cannot wait for ink that has not arrived yet.

## The controls

Four, each proven by running it.

All four are durable and all four run in `scripts/latency_delay_sweep.sh`,
which is wired into `make test`. Each asserts the harness's EXIT STATUS as well
as its diagnostic, because a harness that prints an error and exits 0 has to
make the control fail rather than pass.

| control | what it plants | what it must say |
|---|---|---|
| over budget | `submit` at its real budget of 12, +10 frames | `expect_latency submit: 16 frames from input to first ink, budget 12`, nonzero exit |
| no outcome | watch ink the input never produces | `'zzz-this-ink-never-appears' never appeared`, nonzero exit |
| preexisting target | arm on a target already on screen | `is ALREADY present — this arm would measure the runner's pacing`, nonzero exit |
| observer calibration | +7 frames, every arm | every arm moves by exactly +7 |

**Be precise about what the calibration proves.** `HANABI_LATENCY_DELAY` holds
the OBSERVATION back N frames after the ink appears; it does not slow the app
down. So it proves the instrument's arithmetic — each arm reports
`settle - input` from its own watched target, and no arm is reading a constant
or another arm's number. It is not evidence that the app is slow, and the
over-budget control is what proves a ceiling can actually fail.

## Attachment is the production path

`mark`/`settled` calls in app code were tried and removed. The arm watches
`attach_chip_0`: the input is a real picker choice, the file goes through the
real retain path in `attachment_intake_system.h`, and the chip is drawn through
the same decode cache the transcript's inline images use.
`HANABI_PICK_FILE_TEST` only replaces the native dialog — the same seam
`tests/ui/composer_file_picker.e2e` drives — and everything after it is
production code.

## Popup and sheet are measured apart

A popup is drawn over the pane it belongs to. A modal takes the window, scrims
what is behind it, and takes the keyboard. Different amounts of work, so
different arms and different budgets: `popup_open` is the slash menu,
`sheet_open` is the rename modal reached by right-click → menu item.

## Scroll needs its own fixture

Every hand-written thread in the mock fits on screen at 1100x760, so a scroll
arm on one of them settles never — a fixture that proves nothing rather than a
fast app. The 120-turn perf fixture is the one place there is something to
scroll, which is why scroll is a third script.

## Not covered, and why

- **Cmd-chord overlays.** The harness cannot send a chord, so the slash menu is
  measured instead: a real popup on a real user path, exercising the same
  build-an-overlay-over-a-live-thread work.
- **Threading.** No experiment was run or retained. Every arm is 1–7 frames, so
  there is no stall for a worker to relieve, and ECS, UI, layout, input and
  render all stay on the main thread.
