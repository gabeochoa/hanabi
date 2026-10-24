# The scriptable debug driver (kt-h26z)

kt-h26z is the 6.9 epic asking for "a scriptable debug driver so an agent
can measure a gesture": drive resize, scroll, drag, tab switch and pane
focus from a script, and read back frame timing. On Hanabi that driver
already exists -- it is the scripted-UI harness, `tests/ui/*.e2e`, run
against `output/hanabi_uitest.exe` (`zig build uitest`, or
`HANABI_UI_TESTS=<dir> bash scripts/run_ui_tests.sh` for a subset). This
doc is the one-mechanism map the epic asks for: which command drives
what, which command measures it, and which assertion proves the driver
actually moved the thing.

## Safety, by construction

- **Not in the release binary.** The driver is the test harness
  (`hanabi_uitest.exe`), a separate binary. The shipped app has no script
  input, no control socket, no defaults-driven commands. Stronger than the
  epic's "compile-time gate": there is nothing to gate.
- **Never reachable from the wire.** Scripts run in-process from files on
  disk; no socket, no server state. A statement about the screen never
  becomes server state.
- **No TCC-gated APIs.** The driver injects synthetic input inside the
  process (afterhours test-mode input); the `native_*` commands post real
  NSEvents through NSApp from inside the app. No CGEvent posting, which
  is what TCC would block.

## Drive

| epic verb | command | notes |
|---|---|---|
| `resize W H` | `resize W H` | Hanabi consumes it ahead of afterhours' builtin: the resolution moves now, the render target at the next frame boundary (`src/util/gfx_resize.h`). A real AppKit live resize is `native_drag_resize dw dh [steps]` (windowed scripts only). |
| `scroll PANE DELTA` | `scroll_wheel DX DY` | Wheel events into the focused scroll view; `mouse_move X Y` first to place the pointer. |
| `drag FROM TO` | `drag`, `drag_to`, `mouse_down` / `mouse_move` / `mouse_up` | Row drags, splitter drags, slider drags (`set_slider`). |
| `focus PANE` | `focus_ui <name>`, `click_ui <name>`, `key tab` / `shift_tab` | Assertions: `expect_focused`, `expect_not_focused`, `dump_focusable`. |
| `open TAB` | `open_thread <id>`, `split_tab`, `move_tab` | Assertions: `expect_open`, `expect_tab`, `expect_panes`. |
| `open` / `close` | `open_companion`, `open_bug_report`, `native_click_text` | Sidebar rows, tabs, panes. |

Coordinates are **points**, never device pixels (the epic's rule 5): the
harness letterboxes points against the resolution it holds, and
`HANABI_UI_SCALE` is the only scale in play.

## Measure

| what | command / gate |
|---|---|
| event -> first ink, in frames + us | `watch_ink <label> <outcome> <target>` then the gesture, then `expect_latency <label> <max_frames>` (src/util/latency.h; budgets in `tests/ui/interaction_latency_*.e2e`). The `[latency]` stderr line carries the reading. |
| a resize's frame timing | `scripts/resize_drive_gate.sh` -- a real AppKit live resize with per-size CADENCE + `[prof]` tables (docs/perf/RESIZE.md). |
| idle frame timing | `scripts/cpu_audit.sh` (`IdleTiming` + `IdleWhy`), `zig build idle-gate`. |
| a gesture under load | `scripts/stress.sh` (SCENARIO/FRAMES/EVERY/SESSIONS/UNTIL), `scripts/soak.sh`, `scripts/stress_resize_gate.sh`. |
| launch | `scripts/measure_launch.sh`, `scripts/launch_big_catalog_gate.sh`. |

Deliberately NOT in-script: afterhours' `perf_commands`
(`dump_profile`, `expect_fps_above`, `expect_p99_below`) are opt-in and
Hanabi does not register them -- `AFTERHOURS_ENABLE_PROFILING` is off, so
the collector would report nothing. Frame-time numbers come from the
gates above, not from a script command. Wiring the collector in would
instrument every system every frame in the test binary; that is future
work, not this knot.

## Prove the driver moved the thing (the epic's rule 4)

A green run under a driver nobody proved is wired is not evidence. Every
measurement script carries its control arm -- an assertion that fails if
the gesture silently did nothing:

- resize: `expect_resizes_applied 0` before, `expect_resizes_applied 1`
  after. The counter reads resizes applied **at a frame boundary**; it
  fails both ways (deferral regressed -> 0; command unconsumed ->
  process aborts on the builtin path).
- scroll: `expect_scroll_y <name> <y>` reads the scroll view's offset.
- any gesture measured by latency: `expect_latency` fails if the ink
  never changes, which is exactly what a dead driver produces.
- tabs/panes: `expect_open` / `expect_panes` read the pane's selected
  session, not a highlight.

## Worked loop

`tests/ui/driver_measures_a_resize_and_a_scroll.e2e` drives the epic's
loop in one script: a scroll gesture measured by `watch_ink` +
`expect_latency`, a resize proved moved by `expect_resizes_applied`,
a tab switch proved by `expect_open`.

## What the epic's Puffin sub-knots became on Hanabi

The stress half's Puffin knots (kt-mlbv text layout in a resize frame,
kt-vz0u second-long resize stalls, kt-3j4a transcript re-measure per
width, kt-pui0 three layouts per frame, kt-g4hh PaneWidthLevel,
kt-9j9w sidebar-resize harness) are SwiftUI/Puffin mechanisms. Their
Hanabi equivalents live in docs/perf/RESIZE.md (transcript
+natural-width rule, same-step draw), `scripts/resize_drive_gate.sh`,
`scripts/stress_resize_gate.sh`, `scripts/viewport_bound_gate.sh` and
`scripts/scroll_gate.sh`. Tab stress (520 sub-agent tabs) and the
1000-thread TTI are `scripts/stress.sh` scenarios and the
`subagent_index_gate` / `tab-persistence-gate` gates.
