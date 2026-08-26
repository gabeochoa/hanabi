# hanabi — feedback from Gabe, 2026-08-26

Live list. He is testing the build (`063bebc`) and sending items as he finds
them, from inside hanabi itself. Nothing is started until he stops, except
where marked.

Status key: `TODO` · `IN PROGRESS` · `DONE` · `WONTFIX (reason)`

## Every item files its gap

His instruction, 2026-08-26: *"make sure to write everything you need into
afterhours gaps so i can go look into it"*.

So each item below has two deliverables, not one: the fix in hanabi, and — where
the library is what made it hard — an entry in `afterhours_gaps.md` written for
someone who will go read it cold and decide whether to change afterhours. The
gap is the more valuable half: hanabi is one consumer, and ~20 projects vendor
this library. `vendor/afterhours` stays READ-ONLY; the gap is how the finding
gets out.

A gap entry earns its place by naming the MECHANISM — the specific call, type or
lifetime rule that forces the workaround — not the symptom. The bar this session
has been holding: #115 was filed claiming the app was stuck, and the claim was
wrong because of a single word (`existing_ui_elements` is public, not private);
it had to be retracted and rewritten. Prefer no entry to a confident wrong one.

Gap numbers: the file is at **151 entries** and its highest live number is
**#232**. Take **#240 and up** for this batch, one range per work item, so
parallel work does not collide. NOTE a pre-existing wart: entries #27–#35 appear
TWICE in the file (around lines 852 and 1111) from an early era — do not
renumber them, source cites those numbers, but do not add to the confusion.

Items whose gap is likely the whole point:
- **A1–A6** — whatever makes a four-role transcript the shape of least
  resistance. This is the biggest one on the list.
- **B1** — what a single-line text_input costs to turn into a text area, and
  what breaks in the turning.
- **B2/B3** — no word-delete, no select-all: what the input plugin does and
  does not give a consumer, and whether a consumer can add them without
  reimplementing editing.
- **C1** — #83 already exists for `:focus-visible`; this is its second act, and
  should say what the ring's GEOMETRY cannot be told (offset, radius, which
  element).
- **C2** — containment: a widget drawing outside its own rect.
- **C6** — dragging a custom-drawn surface; see #147 (a scroll view is
  reachable only by debug name) and #138 (a mark per item is linear in
  allocations).
- **E1** — whether anything in the library supports two independent view trees
  in one window.

---

# A. Blocking — the client cannot show what a session emits

These are almost certainly ONE root cause: `api::Role` is
`{User, Assistant, System, Tool}` (`src/api/types.h:18`), so every event that
is not one of those four has nowhere to land. The transcript renders a narrow
subset of a real session and silently drops the rest. Fixing the type is the
first move; each item below then needs its own rendering.

## A1. His messages to me are not visible — TODO
"i cant see your messages to me you need to test and validate that works"
The one that makes the app unusable rather than rough. Must end with a real
end-to-end check against a live session, not a mock.

## A2. Thinking is missing — TODO
## A3. Deliveries are missing — TODO
## A4. Subagents are missing — TODO
"i dont see any subagents"
## A5. Nodes are missing — TODO
## A6. Skills are missing — TODO

---

# B. Input and keyboard

## B1. No Shift+Enter for a newline — TODO
"theres no shift+enter to add newlines in the message"
Confirmed in source: `main_pane_system.h` ~5054 states it outright — single-line
composer, plain Enter sends. afterhours has a multiline text-area config
(`component_config.h:178`). Moving to it must preserve: Enter-to-send, the
send-key setting (Return vs Cmd+Return), draft persistence per thread, and
up-arrow history recall.

## B2. Alt+Backspace does not delete a word — TODO
"alt-backspace doesnt work, i thought we added these already?"
He believes this was done before. No match for alt/word-delete anywhere in
`src/keys.h` or the ECS systems — so either it never landed or it landed and
regressed. Find out which and say so.

## B3. Cmd+A does not select all text — TODO

---

# C. Visual and layout

## C1. Focus rings need a lot of work — TODO (raised twice)
"the focus rings are really bad" … "the focus rings need a ton of work"
Mine from this session: `src/ui/focus_visible.h` +
`src/ecs/focus_visible_system.h`, built because afterhours rings whatever holds
focus however it got there (gap #83). My rule: off until a nav key, off again
after a pointer press. Raising it twice means it is not a small miss. Suspects:
arming on ANY of Tab/Up/Down/Left/Right (arrow keys inside a text field arm it);
1px hairline in `theme.focus_ring` blue, drawn flush, ignoring the widget's own
corner radius; possibly painted on several elements or the wrong one.

## C2. Many buttons render outside their bounds — TODO
Needs the specific buttons; likely a general containment bug worth one fix.

## C3. Typing indicator is positioned wrong — TODO
"the position of the typing indicator doesnt line up where it should"

## C4. Pinned threads are not in the sidebar — TODO
"pinned threads arent there"
The sidebar has a whole pinned-prefix sort pass (`sidebar_system.h` 2507-2512,
2775) — so the ordering logic exists and something upstream of it is wrong.

## C5. The Steer button should be an icon — TODO

## C6. Allow dragging along the minimap — TODO
Scrub the transcript by dragging the minimap, not just clicking it. Note gap
#138: the minimap's per-message mark costs an entity each, so a drag that
rebuilds marks per frame is the expensive shape — build the interaction so it
does not.


## C7. Mouse wheel must scroll a thread — DONE (`feat/transcript-wheel`)

His words, 2026-08-26: *"can you make sure that scrolling with the scroll wheel
works in threads, i would love that feature"*.

It did not half-work. Over a transcript the wheel moved the view **exactly zero
pixels** — eight notches, `transcript_bottom_pad` at y=646 before and y=646
after — and it was not a dead event or a missed hit-test, because the same eight
notches after one PAGE_UP moved the pad 646 → 1209 → 1689. The notch arrived
every time and the pane erased it on the next frame.

Two lines of the follow-latch, in this order:

```
if (prevOffset >= 0 && offset < prevOffset - 2) follow = false;
if (nearEnd) follow = true;              // undoes the line above
```

afterhours writes a notch to `scroll_target` and eases `scroll_offset` toward it
at 0.28 of the distance a frame, so one frame after a notch the offset has moved
5.6 px — inside the 24 px `nearEnd` band. The latch broke and re-armed on
consecutive lines, the pane re-pinned to the end writing BOTH fields, and the
offset could never leave the band. Every thread opens pinned to the end, so the
wheel was dead everywhere a reader actually is.

Fixed by reading the reader's intent off `scroll_target` — the field the WHEEL
writes — instead of `scroll_offset`, the field the EASING writes
(`src/ecs/follow_latch.h`, `tests/unit/test_follow_latch.cpp`). Scrolling up
lets go of the bottom, scrolling back to the end pins again, and both panes of
a split work.

STILL NOT RIGHT, and it needs his hardware to settle: a trackpad or Magic Mouse
scrolls twice as far as the finger, and a wheel detent moves one line where the
Mac convention is three. One multiplier, two conventions —
`afterhours_gaps.md #405`. `HANABI_SCROLL_SPEED` is the knob until then.

---

# D. Audits he asked for

## D1. Look up how search works — TODO
Read it and report; scope unstated, so start with a plain explanation of the
current behaviour and where it is wrong.

## D2. Verify all ~800 commit descriptions against their diffs — TODO
"read through all 800 commits and make sure each one description is supported"
821 commits in the repo. Every claim in every message checked against what the
commit actually changed. Parallelizable and mechanical; the deliverable is the
list of messages that overstate, misattribute, or contradict their diff.
I have already found two of my own this session (a 635px line width that was
not a line width; gap #115's claim the app was stuck, which one wrong word
caused) — so the yield here is real.

---

# E. Features

## E1. Pane splitting — TODO
Split the main pane so two things are visible at once. Needs a decision from
him on the shape before building: split which axis, how many panes, does each
pane hold its own thread (and therefore its own tab strip / scroll position /
draft), and is the split per-window or per-tab. This is the one item on the
list that sets a direction hard to redirect later — the pane owns scroll state,
draft state, and the transcript render cache keys, all of which are currently
single-pane assumptions.

---

## Housekeeping

- Worktree cleanup: 86 of 87 merged worktrees being removed.
  `~/w/hanabi-bold` (`feat/bold-face`, 3 unmerged commits, weighted font
  resolution) kept back pending his call.
