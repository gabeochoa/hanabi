# Screenshot baselines

Committed reference PNGs, one per screen. `make validate-screenshots` recaptures
these screens and fails if any of them drifts from what is stored here, so an
accidental visual regression shows up as a failing target instead of being
noticed weeks later.

This is chunks 1–3 of `docs/breakdown/screenshot-testing.md`. Three screens are
baselined so far; the rest of the 32-state set lands in a follow-up.

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

`make update-baselines` recaptures exactly the screens that already have a
baseline. To add a new one, name it:

```bash
make update-baselines SHOT_FILTER='^04_transcript_light$'
```

Then give it an entry in `manifest.json`.

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
