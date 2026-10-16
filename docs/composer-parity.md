# The composer, against the reference's 0.7.1

Source of truth: the user-supplied screenshot of 2026-09-12 (two split panes,
a composer under each; kept out of the repo -- it carries a private
conversation) and the reference's source for what each control MEANS
(`ComposerPane.swift`, `ComposerControlStrip.swift`, `StripFit.swift`,
`ChildRow.swift`, `NodeAttachment.swift`, `SkillUse.swift`,
`PlanChipText.swift`, `SessionContext.swift`, read at master the same day).
Nothing below was inferred from an icon.

## What the reference draws, and what hanabi draws now

| reference | semantics (from source) | hanabi | status |
|---|---|---|---|
| one composer per pane | each pane's field targets its pane's thread | `render_composer(pane)` twice in a split; own draft, attachments, brake, steer/stop, popovers per pane; the focused pane's is the active one | done |
| strip above the field | `ComposerControlStrip.controlStrip`, in this order | same order, same rungs | done |
| sub-agents chip | `ChildRow.label`: "N running" while any child runs (accent), else "N"; units by rung | `sub_agents_chip_label`, accent when running, opens the sub-agent sidebar | done |
| nodes chip | `NodeAttachment.chipLabel`: a COUNT ("2" / "2 nodes") | count (was the node's name); opens the picker | done |
| model (effort) | `ModelChipLabel`: name in muted ink, " (effort)" at 0.7 opacity | model label muted; effort chip beside it (both open their pickers) | done |
| context reading | `SessionContext.windowPercentLabel`: used / context window | hanabi's tokens-over-compaction-budget figure (`ContextUsage`); the adapter carries no window | kept as is -- a percent would be invented |
| skills chip | `SkillUse.chipLabel` over `invokedSkills`, distinct | distinct `Skill` events in the transcript | done |
| plan chip | `PlanChipText.label`: "a of b" | `SessionPlan::chip_label()` → "2 of 5" | done |
| companion avatar pill | companion mode, a preference | hanabi has no companion mode | not drawn |
| disclosure chips (Tools / Thinking / Deliveries) | drawn only when `DisclosureChipVisibility.isShown` | the tool-fold chip stays (no preference yet; five scripts drive it), in the chip face | deviation, documented |
| attach control | none -- drop and paste | drop and paste (AttachmentIntakeSystem) AND a bare paperclip glyph at the strip's right (the one pointer path to the picker) | deviation, documented |
| field | one 25% hairline, radius 6, same focused or not; caret says focus | same; the accent focus edge is gone | done |
| placeholder "Message Agentcloud… (⏎)" | | "Message hanabi…" -- the key glyph is not in the face (gap #48) | as before |
| microphone | `ComposerDictationControl`, only when dictation is available | no dictation in hanabi | not drawn (no fake mic) |
| send / stop / wait | `sendButton`: arrow (highlight when canSend) / `stop.circle.fill` when running with nothing staged / spinner while creating | arrow (primary when sendable), steer mark with text while running, STOP when running and empty (new `Client::interrupt_session`, `{"cmd":"interrupt"}` bare), ring while the stop is on the wire | done |

## The rungs

`StripFit.swift`: the rung a width buys is the best whose threshold it meets
(full 902, chipGlyphs 777, countsOnly 533, bareCounts 490, plainModel 414);
a flag is "the rung is better than the one that sheds it". So the unit
survives to 777 ("2 nodes"), the state word to 533 ("1 running"), the
model's detail to 490. `src/ecs/composer_strip.h`;
`tests/unit/test_composer_strip.cpp` holds every label to the source at the
boundaries. The user's panes (~690 wide) sit at counts-only: "137", "2",
"1 running", "0 of 4".

## Stop, on the wire

The reference's `AgentcloudSocket.interrupt()` transmits `{"cmd":"interrupt"}`
(optional `text`) on the attached session socket and returns; there is no
ack -- the run's end arrives as frames. hanabi's `interrupt_session` does the
same over a fresh attach (`agentcloud_client.cpp`); steer is the same verb
with text (`input` + `apply: interrupt`). Failure modes: no token / no hello
/ attach refused / socket closed before send → `Result::failure`, shown in
the notice row's command slot; success is "sent", and the thread's state is
adopted only when the list (or the live stream) says it stopped
(`interruptSettlingId`). Nothing is applied optimistically.

## Two composers

Each pane's composer is `render_composer(ctx, uiRoot, app, paneIndex, …)`
with `composer_target_for(paneIndex)`; the bar's `mk` is salted by pane
(gap #596). A submission is taken by the composer of the pane it targets. The
caret landing in a field makes that pane the focused one. The focused pane's
widgets keep the plain debug names (`composer_reply_input`, `composer_send`,
…); the other pane's carry `_other`, and the names swap with the caret.
Scripts: `two_composers_keep_their_own_drafts`,
`stop_in_the_other_pane_stops_that_pane`,
`a_narrow_split_keeps_both_composers`,
`stop_targets_its_own_pane_and_keeps_the_draft`,
`attachment_stays_with_its_split_pane`, `attachment_send_keeps_split_owner`.

## Send chords (experiment, default off)

Settings › Chat › "Send chords (experiment)". The web's composer, read from
the parity review extract (R5, R10), gives Return three chords while a turn is
running: ⌘⇧Return flips steer↔queue for that one message, ⌘⌥Return sends now,
and ⌘Return opens a new chat. Hanabi does NOT take that table, and a reader of
this tree should know why without the commit log:

- **⇧Return is a newline before anything else sees it.** The afterhours text
  area turns Shift+Return into an inserted line break
  (`text_area.h`, `config.text_area_submit_on_enter` with the Shift check) and
  never calls the submit listener, so a Shift chord cannot reach the send.
  Hanabi's own "New line with" setting rides the same key, and the reference's
  ComposerReturn also reserves ⇧↩ for a newline.
- **Ctrl is Cmd under the harness.** The scripted UI cannot hold Super; its
  `CMD+` becomes Ctrl (afterhours gaps #49, #256), so a Ctrl chord would be
  indistinguishable from a Cmd one in every test we can write.
- **"Send now" is what plain Return already does.** Every mid-run steer is sent
  with `interrupt = true` (the loader sets it for the steer path), so a
  separate send-now chord would be a second name for the same send.
- **⌘Return stays the send key.** The extract itself flags ⌘↩-as-new-chat as a
  change to the Return table; Hanabi keeps ⌘↩ as the ordinary "Send message
  with" alias and adds no meaning to it.

What the experiment adds is therefore one chord: **⌘⌥Return queues this one
message** for the end of the running turn (the path `/queue` uses), while
plain Return keeps the existing policy (steer while running, send when idle).
The chord is classified at submit from the live modifiers
(`ui/send_chords.h`) and rides the `ComposerSubmission`. With the experiment
on, hovering Send shows a two-row card instead of the one-line tip — Return
"Steer this one", ⌘⌥↩ "Queue this one" (idle: Return "Send") — each chord
drawn as keycaps by the shared keycap row. Off, nothing changes: no chord is
read and the tip is the usual one. Scripts:
`send_chords_queue_this_one_when_the_experiment_is_on`,
`send_chords_do_nothing_when_the_experiment_is_off`,
`the_send_button_teaches_its_chords_with_keycaps`.

## Icons

Five Lucide glyphs added to the atlas (`waypoints`, `server`,
`wand-sparkles`, `list-checks`, `paperclip`). `scripts/gen_icons.py` keeps
the committed pixels of every icon already in the atlas and rasterizes only
new names (qlmanage cannot start under the build machine's sandbox, so
`scripts/tools/svg2png.swift` draws the SVG through AppKit); the 23 existing
cells are byte-identical, so no sidebar baseline moved for this.
