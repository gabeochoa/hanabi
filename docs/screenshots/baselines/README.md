# Screenshot baselines

Committed reference PNGs, one per screen. `make validate-screenshots` recaptures
these screens and fails if any of them drifts from what is stored here, so an
accidental visual regression shows up as a failing target instead of being
noticed weeks later.

`scripts/screens.sh` can capture 35 states and all 35 are baselined here. There
is no `unbaselined` list any more; the reasons the last one held are in "What
the pinned clock fixed" below.

## What went wrong the last time, and what stops it recurring

Read this before touching anything here. It is the whole reason the set was
re-cut.

**Symptom.** `make validate-screenshots` failed 30 of 30 at 11%-100% differing
pixels, on screens like `18_auth_dark` and `15_settings_dark` that nothing in
the intervening work had gone near. A set that fails everywhere reads like a
broken harness, and it was not one.

**Two independent causes, both real.**

**1. The baselines were 285 commits stale.** They were cut on 2026-08-23 at
`1cbbcd4` and never refreshed while the theme palette, the corner roundness,
the fonts, the layout and the icon atlas all moved. The dark screens read
~100% differing because a dark UI has almost no pure-black pixels: the window
background alone went `(11,11,13)` to `(10,10,16)`, which is three levels and
84% of `18_auth_dark`'s area. The light screens read 11-46% because their large
flat white regions were unchanged. That split — dark near 100%, light in the
teens — is what a *palette* change looks like, and it is worth recognising: a
1x/2x scale mismatch or a colour-space difference would have moved every screen
by the same enormous amount, and a real regression moves one screen a little.

**2. The set was rotting by the clock, and nobody had measured it.** The
manifest recorded two states as unstable across the day. The real number was
**17 of 35**. Every transcript screen gained or lost a date divider as local
midnight swept through the mock's `now - N` fixture, and a hover bar printed an
absolute `HH:MM`. Measured by capturing the full set at several local hours on
one machine and one build:

```
03_transcript_dark      9.26% differs across local hours
26_thinking_dark       13.11%
27_streaming_dark      13.73%
22_split_view_dark      7.37%
...  17 states in total, 14 of them baselined
```

So the set reproduced for an hour or two after it was cut and then stopped, and
the failure looked identical to a real regression. That is worse than no net:
it is the reason a 30-image compare could go 30-for-30 red on a tree nobody had
broken, and the reason nobody believed it when it did.

## The environment these baselines are valid for

A baseline is an image plus the conditions that reproduce it. These are the
conditions, and only the first three are pinned by the harness itself.

| | value | pinned by |
| --- | --- | --- |
| capture epoch | `1781524800` (2026-06-15 12:00:00Z) | `scripts/screens.sh` |
| timezone | `UTC` | `scripts/screens.sh` |
| backend | `mock`, isolated `HOME`, `HANABI_CONFIG` at a path that does not exist | `scripts/screens.sh` |
| resolution | 1100x760, 8-bit RGBA, **1x** | `scripts/screens.sh` rejects any other size |
| machine | `gabeochoa-mac-GRQ7Y259H4`, Apple M4 Max, macOS 26.6 (25G72) | **not pinned** |
| renderer | Metal via sokol, headless offscreen target, no supersampling | **not pinned** |
| build | `-O2`, TLS auto-enabled when OpenSSL is present | **not pinned** |
| comparison | Pillow 11.3.0, or ImageMagick 7 as a fallback | **not pinned** |

The first four make the images independent of *when* they are captured. The
last four do not travel: another machine, another GPU driver or another
rasteriser can move an anti-aliased glyph edge by a level, and this set is at a
0.0% threshold.

**The Retina question, since it will come up on this box.** These are 1x
images. The windowed app runs `high_dpi=true` and is crisp on the panel, but
the headless `--screenshot` path renders into a fixed 1100x760 offscreen
texture that the Metal backend does not supersample, so the display's scale
factor never reaches the capture. That is why the 100%-differing failure above
was *not* a 1x/2x mismatch, and why plugging in a different monitor will not
break this set. (`afterhours_gaps.md` #101, #92; `src/main.cpp`'s hi-DPI note.)

## How to tell when they have stopped being valid

Three checks, cheapest first. All of them answer "is the harness still telling
the truth", which is a different question from "did the UI change".

```bash
make test-screenshot-determinism   # ~10 s. Two captures of one screen, byte-identical?
make validate-screenshots          # ~50 s. All 35 against what is committed.
make gate-audit DEFECT=shots.composer_grey   # ~2 min. Does the net still go RED?
```

- **Two captures of the same screen differ.** The render is not deterministic
  any more and nothing here can be trusted until it is. Something reached the
  wall clock, an animation is not frozen, or a fixture is seeded from an
  absolute epoch. Do not widen a threshold; find it.
- **Every screen fails at once, by a lot.** A palette, font or scale change, or
  a capture taken somewhere other than the machine in the table. Look at the
  numbers: if dark screens are near 100% and light screens are in the teens,
  that is a palette move (see above), not a broken harness.
- **A few screens fail by a little.** The ordinary case. Open
  `test-failures/<name>-diff.png` and judge it.
- **`make gate-audit DEFECT=shots.composer_grey` comes back green.** The worst
  outcome on this list, because it is the one that looks fine. The net has
  stopped asserting; treat it as broken, not as passing.

If a re-capture on a *different* machine is what you need, capture the whole
set there, commit it, and add a row to the table above. Do not mix: a set half
from one box and half from another has no defined environment at all.

## What the pinned clock fixed

`scripts/screens.sh` exports two variables that between them remove time from
the render:

- `HANABI_MOCK_NOW` fixes the **datum** — every stamp the mock hands out
  (`src/api/mock_client.h`, `now - N`).
- `HANABI_CAPTURE_EPOCH` fixes what the renderer **measures it against**
  (`src/util/capture_clock.h`): the frozen durations it already pinned, plus
  now the relative ages and the date labels.

Both are set to the same integer, which is the point. Freezing only the display
side was deliberately avoided before, and rightly: the mock seeds its stamps
after the freeze, so a pinned display against a live datum makes every age a
fraction of a second short — enough to round `now - 3h` down to "2h". Pinning
both to one constant removes the skew rather than splitting it. `TZ` is pinned
to UTC in the same place, because a date divider and a "14:05" are local.

Nothing outside the screenshot harness changes. `scripts/soak.sh` and
`make stress` set `HANABI_MOCK_NOW` and not `HANABI_CAPTURE_EPOCH`, so their
rendered ages are exactly what they were.

Five states used to be unbaselineable and are not any more:

| state | was | now |
| --- | --- | --- |
| `07b_hover_msg_copy_dark` | the hover bar prints an absolute `HH:MM`, so it changed every minute | the epoch is fixed, so the string is |
| `20_big_transcript_dark` | a date divider swept into the visible tail as local midnight passed through the rbig fixture | same |
| `32_new_messages_dark` | same rbig fixture | same |
| `06_hover_row_star_dark` | byte-identical to `01_home_dark`: the hovered row was inside a collapsed folder | the row renders; the two differ over the star's 12x11 box |
| `24_thread_loading_dark` | byte-identical to `03_transcript_dark`: the mock resolved before the loading state could be seen | the loading state survives to the settle frames |

And one that *was* baselined was photographing the wrong thing:
`28_composer_focus_dark` was byte-identical to `03_transcript_dark`. The capture
hook set keyboard focus, but `src/ui/focus_visible.h` keeps the focus ring off
until Tab has been pressed — the `:focus-visible` rule — and a headless capture
presses nothing. So the one baseline whose job is to watch the composer's focus
ring had no focus ring in it. The hook arms focus-visible now. This mattered:
the ring is half of the regression in `afterhours_gaps.md` #263.

A duplicate check is worth running by hand after adding a state, since nothing
automates it yet — all 35 renders currently have distinct bytes.

## The rules that keep this honest

**Baselines come from the mock backend.** `scripts/screens.sh` forces
`HANABI_BACKEND=mock` and points the runtime config at a path that does not
exist. A real backend serves live data, which would put someone's actual
conversations into a committed PNG and change the image on every run. Never
capture a baseline any other way. Reading the diff is not enough — look at the
images: all 35 were inspected before they were committed, and the only account
names, URLs and sign-in codes in them are the mock's `example.invalid` fixtures.

**Thresholds live in `manifest.json`** — a default plus a per-screen override,
as a percentage of differing pixels. All 35 are at 0.0%. That is earned, not
optimistic: the full set was captured twice back to back and under four
different host timezones, and all 35 came back byte-identical every time. A
threshold above 0.0% here would be hiding a flake, not tolerating one — a state
that cannot reproduce should be fixed or dropped, and both are now cheap.

**Every state is accounted for.** Validation fails on a state `screens.sh` can
capture that is neither baselined nor named in `manifest.json`'s `unbaselined`
map, so a screen added to the harness cannot quietly go unchecked.

**A capture that photographs the wrong state gets no baseline.** Two states
were duplicates of other screens for months and a third was a duplicate while
claiming to watch a focus ring. A baseline under those names reports coverage
of something nobody is looking at and goes green forever, including after the
knob is fixed.

**Durations inside a capture are frozen, not tolerated.**
`src/util/capture_clock.h` pins one clock reading for the whole headless
capture and pins the phase of the pulsing dot, so `26_thinking_dark` is
byte-stable even under `HANABI_FRAME_TIMING=2000`, which stretches the run by
two seconds on purpose.

## Where it runs

There is **no CI runner for this repo** — the remote is a personal GitHub repo
with no `.github/` on any branch or in the history. So:

| | screens | cost | when |
| --- | --- | --- | --- |
| `make test` | the 8 in `SHOT_FAST` | ~11 s | every run |
| `make validate-screenshots` | all 35 | ~50 s | before a push; `make gate` runs it |
| `make gate` | both, plus `make test` | minutes | `make install-hooks` puts it on `git push` |

The subset in `make test` is the fix for how this rotted in the first place. It
was a separate target for months, so nothing ran it, so 285 commits went by
before anyone learned the baselines were dead. The eight are chosen to touch
each thing a rendering change breaks rather than each feature: both palettes,
the digest and the transcript, a sheet over a dimmed backdrop, the folded rail,
the icon atlas, and the composer with its focus ring. The list is `SHOT_FAST`
in the makefile.

## Workflow

```bash
# prove the render is still deterministic (byte-identical repeat capture)
make test-screenshot-determinism

# check the current build against these baselines
make validate-screenshots

# a visual change was intended — adopt the new render
make update-baselines
git diff --stat docs/screenshots/baselines/
git add docs/screenshots/baselines/<the pngs you meant to change>
```

`make update-baselines` recaptures the screens that already have a baseline
**plus** any state that has neither a baseline nor an `unbaselined` entry, so a
newly added state is adopted without naming it. To recapture one screen and
nothing else, name it:

```bash
make update-baselines SHOT_FILTER='^04_transcript_light$'
```

Then give it an entry in `manifest.json`.

The suite ends in a `pkill` scoped to this worktree's own binary, but several
checkouts capture on one machine at a time. Take the shared lock around a batch:

```bash
until mkdir /tmp/hanabi_suite.lock 2>/dev/null; do sleep 2; done
make validate-screenshots; rmdir /tmp/hanabi_suite.lock
```

## A screen with no baseline

Validation only recaptures the states it has baselines for — otherwise a
three-screen check would render all 35. That makes a new state invisible to a
naive comparison: it is never captured, so it is never missed. So the harness
lists what it can capture and the comparison checks the list:

```bash
bash scripts/screens.sh --list     # every state name, one per line; renders nothing
```

A state on that list is either baselined, or named in `manifest.json`'s
`unbaselined` map with the reason it is left out. Anything else is reported and
fails the run:

```
NEW (no baseline): 33_foo_dark
  scripts/screens.sh captures these; nothing checks them.
  Adopt them:  make update-baselines   (then review the PNGs and commit)
  Or record why they stay out, in docs/screenshots/baselines/manifest.json:
      "unbaselined": {"33_foo_dark": "<why this state has no baseline>"}
```

`--lenient-new` reports it without failing, for a branch that adds a state and
baselines it in a follow-up. The reason is deliberately a free-text string.

The fast subset skips this accounting entirely: `--only` names eight screens,
so the run says nothing about the other twenty-seven and does not pretend to.

## When validation fails

A failing run writes `test-failures/` (gitignored) and everything you need is
in it:

```
test-failures/
  15_settings_dark-baseline.png   what is committed
  15_settings_dark-current.png    what this build rendered
  15_settings_dark-diff.png       the baseline dimmed, changed pixels in red
  summary.json                    {passed, failed, total, failures: [...]}
  summary-fast.json               the same, from the make-test subset
```

The current frames are copied there on purpose: they live under `output/` and
the next capture wipes them, so a failure you did not write down cannot be
looked at afterwards.

1. Open the diff image. A few red pixels on a glyph edge is a font or layout
   shift; a red block is a real regression.
2. If the change is intended, `make update-baselines` and commit the new PNGs
   with a reason. If it is not, fix the code — do not raise the threshold.
3. If the screen turns out not to reproduce at all, that is now a bug in the
   capture, not a fact about the screen. Everything that used to be inherently
   unstable here was made stable by pinning the clock; find what is still
   reaching the wall clock.

The comparison is `scripts/compare_screenshots.py`. It uses Pillow when the
running python3 has it and ImageMagick's `magick compare` otherwise; with
neither installed it prints how to install one and exits without comparing.
`scripts/compare.py` is a different tool for a different job — it scores hanabi
against frozen Puffin references with masking and antialias tolerance
(`docs/visual-parity/`). This one wants exact equality, which is why it is the
simpler of the two.

## Proving it can still fail

`docs/perf/GATES.md` section 0 is the standing argument that a gate nobody has
watched fail is not evidence. This net is in that audit:

```bash
make gate-audit DEFECT=shots.composer_grey   # the interior fill, gap #262
make gate-audit DEFECT=shots.focus_ring      # the missing ring, gap #263
```

Both are the regression that motivated re-cutting this set. Observed, with the
eight-screen subset:

```
shots.composer_grey   6 of 8 FAIL at 1.15-2.48%   the six screens with a composer in frame
                      15_settings_dark, 18_auth_dark stay green — no composer on either
shots.focus_ring      1 of 8 FAIL at 0.5289%      28_composer_focus_dark, and only it
```

Which screens went red is the result, not that something did. The grey fill is
on every screen showing a composer and the ring is on exactly one, and the net
reports precisely that.
