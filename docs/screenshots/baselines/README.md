# Screenshot baselines

Committed reference PNGs, one per screen. `make validate-screenshots` recaptures
these screens and fails if any of them drifts from what is stored here, so an
accidental visual regression shows up as a failing target instead of being
noticed weeks later.

This is chunks 1–4 of `docs/breakdown/screenshot-testing.md`. Three screens are
baselined so far; the rest of the 35-state set lands in a follow-up, and every
one of them is listed in `manifest.json` under `unbaselined` until it does.

| baseline | state |
|---|---|
| `01_home_dark.png` | Home digest, dark theme, no tabs open |
| `02_home_light.png` | Home digest, light theme, no tabs open |
| `03_transcript_dark.png` | Transcript with three tabs open (`t2` active), dark theme |

- **Resolution: 1100x760.** `scripts/screens.sh` writes the window geometry into
  the isolated settings file and rejects any capture that is not exactly that.
- **Thresholds live in `manifest.json`** — a default plus a per-screen override,
  as a percentage of differing pixels. All three are at 0.0%: these renders are
  byte-identical run to run, so anything above zero is a real change.
- **Every state is accounted for.** `manifest.json` also has an `unbaselined`
  map: state name → why it has no baseline. Validation fails on a state that is
  in neither list, so a screen added to `scripts/screens.sh` cannot quietly go
  unchecked (see "A screen with no baseline" below).

## The two rules that keep this honest

**Baselines come from the mock backend.** `scripts/screens.sh` forces
`HANABI_BACKEND=mock` and points the runtime config at a path that does not
exist. A real backend serves live data, which would put someone's actual
conversations into a committed PNG and change the image on every run. Never
capture a baseline any other way.

**The mock seeds timestamps relative to now** (`src/api/mock_client.h`,
`time(nullptr) - N`). Datum and display are measured from the same moving now,
so a rendered age stays `3h` forever and a baseline captured today still matches
next month. Reseed the mock with absolute epochs and every time-showing baseline
rots within a day.

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

1. Look at the reported diff percentage. A handful of pixels is usually a font
   or layout shift; a large number is a real regression.
2. Compare by eye: the fresh capture is in `output/screenshots/current/`, the
   reference is here.
3. If the change is intended, `make update-baselines` and commit the new PNGs
   with a reason. If it is not, fix the code — do not raise the threshold.

The comparison is `scripts/compare_screenshots.py`. It uses Pillow when the
running python3 has it and ImageMagick's `magick compare` otherwise; with
neither installed it prints how to install one and exits without comparing.
