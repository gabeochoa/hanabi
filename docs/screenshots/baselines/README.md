# Screenshot baselines

Committed reference PNGs, one per screen. `make validate-screenshots` recaptures
these screens and fails if any of them drifts from what is stored here, so an
accidental visual regression shows up as a failing target instead of being
noticed weeks later.

This is chunks 1–5 of `docs/breakdown/screenshot-testing.md`. `scripts/screens.sh`
can capture 35 states; 30 are baselined here and the other five are listed in
`manifest.json` under `unbaselined`, each with the reason it is left out. Every
state is in one list or the other, and validation fails on a state that is in
neither.

Each baseline's `note` in `manifest.json` says which state it is.

- **Resolution: 1100x760.** `scripts/screens.sh` writes the window geometry into
  the isolated settings file and rejects any capture that is not exactly that.
- **Thresholds live in `manifest.json`** — a default plus a per-screen override,
  as a percentage of differing pixels. All 30 are at 0.0%: two full capture runs
  of this set were byte-identical on all 30, so anything above zero is a real
  change. A threshold above 0.0% here would be hiding a flake, not tolerating
  one — a state that cannot reproduce belongs in `unbaselined` instead.
- **Every state is accounted for.** `manifest.json` also has an `unbaselined`
  map: state name → why it has no baseline. Validation fails on a state that is
  in neither list, so a screen added to `scripts/screens.sh` cannot quietly go
  unchecked (see "A screen with no baseline" below).
- **Every state is accounted for.** `manifest.json` also has an `unbaselined`
  map: state name → why it has no baseline. Validation fails on a state that is
  in neither list, so a screen added to `scripts/screens.sh` cannot quietly go
  unchecked (see "A screen with no baseline" below).

## The rules that keep this honest

**Baselines come from the mock backend.** `scripts/screens.sh` forces
`HANABI_BACKEND=mock` and points the runtime config at a path that does not
exist. A real backend serves live data, which would put someone's actual
conversations into a committed PNG and change the image on every run. Never
capture a baseline any other way. Reading the diff is not enough — look at the
images: every one of these 30 was inspected before it was committed, and the
only account names, URLs and sign-in codes in them are the mock's
`example.invalid` fixtures.

**The mock seeds timestamps relative to now** (`src/api/mock_client.h`,
`time(nullptr) - N`). Datum and display are measured from the same moving now,
so a rendered age stays `3h` forever and a baseline captured today still matches
next month. Reseed the mock with absolute epochs and every time-showing baseline
rots within a day.

**A screen that renders an ABSOLUTE time cannot be baselined at all** — not
today, and not with a wider threshold. `fmtutil::clock_time` turns a now-relative
stamp into "06:08", and `fmtutil::day_label` turns one into "Thursday, August
20"; both change while the datum stays exactly as old as it was. That is why
`07b_hover_msg_copy_dark` (a wall-clock stamp in the hover bar) and the two rbig
screens (a date divider that sweeps into view as local midnight passes through
the fixture) are in `unbaselined` rather than in the set at a threshold big
enough to swallow the digits.

**Durations inside a capture are frozen, not tolerated.** The thinking
indicator's timer is (render now − turn start), two reads of the wall clock a
few frames apart, so it photographed "32s" or "33s" depending on whether the run
crossed a second boundary. `src/util/capture_clock.h` pins one reading for the
whole headless capture, and the same file pins the phase of the pulsing dot;
`26_thinking_dark` is byte-stable as a result, including under
`HANABI_FRAME_TIMING=2000`, which stretches the run by two seconds on purpose.
Only the screenshot path freezes — the app and the scripted UI tests read the
wall clock exactly as before.

**A capture that photographs the wrong state gets no baseline.** Two of the 35
states currently render byte-identical to another screen —
`06_hover_row_star_dark` is `01_home_dark` (the row it hovers is inside a
collapsed folder) and `24_thread_loading_dark` is `03_transcript_dark` (the mock
resolves before the loading state can be seen). Both are in `unbaselined`: a
baseline under those names would report coverage of a state nobody is looking
at, and would go green forever even after the knob is fixed.

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
baselines it in a follow-up. The reason is deliberately a free-text string:
"not baselined yet" and "content is stamped with an absolute time, so a
baseline would rot" are both legitimate, and both are worth reading.

## When validation fails

A failing run writes `test-failures/` (gitignored) and everything you need is
in it:

```
test-failures/
  15_settings_dark-baseline.png   what is committed
  15_settings_dark-current.png    what this build rendered
  15_settings_dark-diff.png       the baseline dimmed, changed pixels in red
  summary.json                    {passed, failed, total, failures: [...]}
```

The current frames are copied there on purpose: they live under `output/` and
the next capture wipes them, so a failure you did not write down cannot be
looked at afterwards.

1. Open the diff image. A few red pixels on a glyph edge is a font or layout
   shift; a red block is a real regression.
2. If the change is intended, `make update-baselines` and commit the new PNGs
   with a reason. If it is not, fix the code — do not raise the threshold.
3. If the screen turns out not to reproduce at all, it belongs in
   `unbaselined` with the reason, not at a threshold wide enough to hide it.

`make gate` runs this check together with `make test`; `make install-hooks`
puts that gate on `git push`. Nothing else runs it — this repo has no CI.

The comparison is `scripts/compare_screenshots.py`. It uses Pillow when the
running python3 has it and ImageMagick's `magick compare` otherwise; with
neither installed it prints how to install one and exits without comparing.
