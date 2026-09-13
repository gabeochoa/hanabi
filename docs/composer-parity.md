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

## Icons

Five Lucide glyphs added to the atlas (`waypoints`, `server`,
`wand-sparkles`, `list-checks`, `paperclip`). `scripts/gen_icons.py` keeps
the committed pixels of every icon already in the atlas and rasterizes only
new names (qlmanage cannot start under the build machine's sandbox, so
`scripts/tools/svg2png.swift` draws the SVG through AppKit); the 23 existing
cells are byte-identical, so no sidebar baseline moved for this.
