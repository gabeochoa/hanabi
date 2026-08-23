# Screenshot Baseline Testing for Hanabi

## Status: Hanabi is deterministic ✓

**Reproducibility proof:** Two captures of the same app state (Home dark theme) produce byte-for-byte identical PNGs (97997 bytes, identical md5). Hanabi renders deterministically frame-to-frame.

**Why:** Animations and interactive state don't leak into screenshot mode because headless capture:
- Renders a fixed 45 frames after layout settles (see `run_headless_screenshot`, line 972 in main.cpp)
- Applies test knobs that either freeze state (HANABI_SKELETON_DEMO, HANABI_LOADING_DEMO) or use fixed points in time (HANABI_THINK_DEMO sets streamStartedAt 32s in the past, so the timer shows "0:32")
- **Relative timestamps are stable, but not for the reason first written here.**
  `std::time()` does NOT stay constant — it is wall clock. What makes "3h" stay
  "3h" is that the MOCK seeds its data relative to now: `mock_client.h:22` says
  "timestamps are now-based (time(nullptr) - N)", and `:401` returns
  `now - h * 3600`. Since both the datum and the display are measured from the
  same moving `now`, the DIFFERENCE is constant, so a baseline captured today
  still matches next week.
- **This is load-bearing and fragile.** If anyone reseeds the mock with absolute
  epochs, every time-showing baseline starts drifting and the suite rots within
  a day. Worth a one-line comment at `mock_client.h:22` saying the screenshot
  suite depends on it.
- Caret blink and spinner animations advance by `dt=1/60` each frame; by frame 45 (~750ms), they've cycled through their periods and settle on a consistent visual state (or can be explicitly frozen via future test knobs if needed)

The mock backend is fully deterministic — it returns the same data every run and doesn't use time-based generation.

**Conclusion:** No changes needed to make hanabi reproducible. Move straight to building the testing suite.

---

## Gaps between wm_afterhours and hanabi

wm_afterhours uses this design:
- makefile targets: `screenshots` (capture to output/), `update-baselines` (write to screenshot-baselines/screens/), `validate-screenshots` (compare with diff detection)
- baseline directory: `screenshot-baselines/screens/` (committed PNG files)
- capture binary: built-in `--headless-screenshots` flag with optional `--image-output` override (all screens; no per-test config)
- comparison tool: `scripts/compare_baselines.py` (PIL-based diff, 1.0% threshold default, per-screen overrides via manifest.json)
- missing baseline handling: script detects new PNGs not in baselines and tells user to run `make update-baselines`

hanabi's situation:
- capture script: `scripts/screens.sh` (29 explicit states, each with per-state settings.json + env knobs; isolated HOME, mock backend forced, robust error handling)
- No comparison or baseline storage yet
- No makefile integration

**Differences that matter:**
1. wm_afterhours has one canonical capture mode (headless path owns all state); hanabi's capture is already highly parameterized (HANABI_VIEW, HANABI_TEST_OVERLAY, HANABI_*_DEMO, plus settings JSON). This is strength — the test states are explicit and auditable — but requires integration that doesn't assume "just run --headless-screenshots".
2. wm_afterhours baselines live in a single git-committed directory; hanabi's screen set (currently 32 PNGs) is live in `/tmp/hanabi_screens` after each run. Need to decide: commit all 32? Gate on coverage?
3. wm_afterhours uses PIL diff; hanabi might need ImageMagick comparison (review_shots.sh already uses `magick compare`). Both work; choose based on dependency availability.
4. wm_afterhours has one output directory (output/); hanabi's resources and content are split (src/, output/, resources/). The comparison must not accidentally capture resource assets or layout artifacts if the canvas size drifts.

---

## Implementation chunks (shipped in order)

### Chunk 1: Prove determinism with a repeat-capture test
**Scope:** Add a makefile target that captures the same screen twice and asserts byte-for-byte equality.

**What ships:**
- New makefile target: `test-screenshot-determinism`
- Inline bash script (no new files) that: captures 01_home_dark twice to /tmp, compares md5 and file sizes, fails if they differ

**How used:**
```bash
make test-screenshot-determinism
```

**Dependencies:** None (uses existing scripts/screens.sh + shell utilities).

**Proof:** md5 match + byte-identical files. If this target passes, every other chunk is safe.

**Estimated:** 20–40 lines in makefile.

---

### Chunk 2: Create baseline screenshot directory and commit structure
**Scope:** Establish where baselines live and initialize the directory with documented intent.

**What ships:**
- New directory: `docs/screenshots/baselines/` (siblings to existing docs/screenshots/review/)
- Placeholder README: documents the intent ("committed reference PNGs, one per screen"), resolution (1100x760), and the update workflow
- First three baseline PNGs: 01_home_dark, 02_home_light, 03_transcript_dark (captures from scripts/screens.sh, renamed into baselines/)

**How used:**
```bash
# After verifying determinism, commit the first set:
bash scripts/screens.sh
cp /tmp/hanabi_screens/01*.png /tmp/hanabi_screens/02*.png /tmp/hanabi_screens/03*.png docs/screenshots/baselines/
git add docs/screenshots/baselines/
git commit -m "Add initial screenshot baselines: home and transcript views"
```

**Dependencies:** existing scripts/screens.sh.

**Why three screens:** Enough to show the pattern (dark/light theming, main views) without committing all 32 at once. Allows chunking the full set into a follow-up.

**Estimated:** 50–150 lines (the README); baselines are binary.

---

### Chunk 3: Comparison script and makefile target
**Scope:** Compare current renders against baselines and report diffs.

**What ships:**
- New script: `scripts/compare_screenshots.py` — Python script that:
  - Takes baseline dir and current dir as args (default: docs/screenshots/baselines/ and /tmp/hanabi_current/)
  - Loads each baseline PNG, finds its counterpart in current dir
  - Uses PIL (if available) for pixel-diff %, or falls back to hash (exact match only)
  - Reports pass/fail per screen with diff%
  - Detects new screens in current dir not in baselines (tells user to run make update-baselines)
  - Exits non-zero if any diff exceeds threshold (default 0.0% — exact match initially)
  - Optional: save visual diff images for debugging (amplified diff overlay)
- New makefile target: `validate-screenshots` — captures current state into /tmp/hanabi_current/ and runs compare_screenshots.py
- New makefile target: `update-baselines` — captures into docs/screenshots/baselines/ directly (for intentional visual changes)

**How used:**
```bash
# After a code change, validate against baselines:
make validate-screenshots

# After an intentional visual change, update:
make update-baselines
git add docs/screenshots/baselines/
git commit -m "Update screenshot baselines for [reason]"
```

**Dependencies:** PIL (optional; exact hash comparison falls back), makefile, existing scripts/screens.sh.

**Integration into make test:**
```makefile
ci: ... validate-screenshots ...
```

**Estimated:** 200–250 lines (compare_screenshots.py) + 20 lines makefile.

---

### Chunk 4: Handle new screens (unbaselined case)
**Scope:** When scripts/screens.sh generates a new screen (e.g., a UI refactor adds a state), the suite must not fail silently.

**What ships:**
- Enhancement to compare_screenshots.py: detects PNGs in /tmp/hanabi_current/ that have no baseline
- Report format: `NEW (no baseline): [names]` + instruction: "Run 'make update-baselines' to add them"
- Optional: allow `--lenient-new` flag to warn instead of fail (for PRs that genuinely add states)
- Makefile: `make update-baselines` always succeeds (adds new + updates changed); doesn't require a commit message because the user reviews the diff before committing

**How tested:** Add a new HANABI_*_DEMO variant to scripts/screens.sh, capture, verify compare_screenshots.py detects it.

**Estimated:** 30–50 lines (Python) + minimal makefile.

---

### Chunk 5: Gather full baseline set
**Scope:** Capture and commit all 29–32 hanabi screens as baselines.

**What ships:**
- All PNG baselines in docs/screenshots/baselines/
- Updated makefile documentation

**How used:**
```bash
make update-baselines
# Review the 29 new files in git diff:
git add docs/screenshots/baselines/
git commit -m "Add complete screenshot baselines for all hanabi views"
```

**Execution:**
- Run `make update-baselines` once
- Verify all 32 PNGs appear and dimensions are 1100x760 (scripts/screens.sh already checks this)
- Commit

**Dependencies:** All previous chunks.

**Estimated:** 0 lines of code (just commit binary files).

---

### Chunk 6: Wire into CI
**Scope:** Add validate-screenshots to the test gate.

**What ships:**
- Updated makefile: add validate-screenshots to the `test` target or a new `ci` target
- Updated CI yaml (if applicable) to run `make test` (or equiv.)

**How tested:** Create a small diff that doesn't change any UI (comment change), verify validate-screenshots still passes. Create a diff that visibly breaks the UI, verify validate-screenshots fails.

**Estimated:** 5 lines makefile + any CI config changes.

---

### Chunk 7: Artifact reporting (optional; post-MVP)
**Scope:** On screenshot failure, save diff images and a JSON summary for CI logs.

**What ships:**
- Enhancement to compare_screenshots.py:
  - `--save-diffs` flag (default: true) — generate amplified diff PNG for each failure
  - `--json <path>` flag — write {passed, failed, total, failures: [{name, diff_pct, baseline, current, diff}]}
  - Failures dir default: test-failures/

**How used:**
```bash
make validate-screenshots  # Diffs auto-saved to test-failures/
# In CI: parse test-failures/summary.json for post-mortem
```

**Estimated:** 80–120 lines (Python).

---

## How this surfaces afterhours gaps

Each screen in hanabi exists to exercise a region of the afterhours UI library. When a screen **can exist but doesn't look right**, it signals a library gap — not a hanabi bug.

### Recording gaps:
1. **Named screens with visual shortcomings.** If a screen's layout is "correct" (no crash, all content rendered) but visually off, document it with a reason in a companion file.
   - Example: `docs/screenshots/baselines/GAPS.md` — entries like:
     ```
     ## 21_tools_expanded_dark
     The nested sub-rows overflow and wrap instead of scrolling horizontally in the
     collapsed "Sub-agents" section. Caused by afterhours #23 (no scroll-anchor /
     preserve-position-on-prepend). Workaround: show one collapsed state.
     ```

2. **Count gaps by severity.** For each test, ask: "Does this screen exercise something the library cannot do?" If yes, it's not a hanabi limitation; it's an afterhours library gap that the screenshot suite now makes **visible and trackable** instead of anecdotal.

3. **Feed afterhours_gaps.md.** As gaps accumulate, propose upstream changes based on how many screens they block or distort. A gap that affects 3 screens is higher priority than one affecting 1.

### Example workflow:
- Capture 15_settings_dark. The modal looks good overall, but the scrollable settings list is clipped without a scrollbar. **This is afterhours #26 (no built-in scrollbar).** The gap is now **visible in CI** alongside the other 14 baseline differences.
- Over time, tally: "6 screens would look sharper with scrollbars, 3 need flex-grow, 2 need text selection." This is data, not opinion. The screenshot suite is the **instrument** that surfaces and counts library shortcomings.

### Integration with afterhours_gaps.md:
Add a new section to afterhours_gaps.md (after the gap list, before proposed upstream changes):

```markdown
## Screenshot Coverage and Gap Visibility

The hanabi screenshot suite (docs/breakdown/screenshot-testing.md) captures 32 canonical UI states. Each baseline is a test — when a screen looks wrong, it's usually a gap, not a bug. The mapping:

| afterhours gap | hanabi screen(s) blocked or distorted | baseline filename(s) |
|---|---|---|
| #23 (no scroll-anchor) | scroll-heavy states lose position | 20_big_transcript_dark |
| #26 (no scrollbar) | modals and panels clipped | 15_settings_dark, 16_settings_light |
| #30 (no scroll-anchor/prepend) | load-older snaps to top | (design prevents capture; happens during interaction) |

This table is maintained as the screenshot suite grows and gaps accumulate.
```

---

## Summary and next steps

**Current state:** Hanabi is deterministic. Its screenshot harness (scripts/screens.sh) is thorough and robust.

**Path to MVP:** 7 chunks, ~800–1100 lines of code (mostly compare_screenshots.py), 0 dependencies on wm_afterhours. Chunks 1–3 are the minimum viable suite (prove determinism, store baselines, compare and report). Chunks 4–6 add completeness and CI integration. Chunk 7 is polish for debug artifacts.

**What makes this valuable:** Every screen is a mini-test of the afterhours library. When a screen looks wrong, it's either a real hanabi bug (fix hanabi) or an afterhours gap (record and propose upstream). The suite makes gaps **visible, countable, and prioritizable** instead of anecdotal complaints.

**Ship order:** 1, 2, 3 (MVP) → review & test → 4, 5 (complete baseline set) → 6 (CI gate) → 7 (debugging polish).

---

## Reference: wm_afterhours design (for copy-paste guidance)

Compare logic from `/Users/gabeochoa/p/wm_afterhours/scripts/compare_baselines.py`:
- Two-arg compare function (baseline, current) returning diff% 
- PIL fallback to hash (0% or 100%)
- Manifest.json for per-screen threshold overrides
- Failures list with structured JSON output
- Clear exit code (0 = pass, 1 = fail) for CI

Makefile targets (relevant lines 279–310, 288–330):
- `screenshots:` runs `./output --headless-screenshots`, then verifies to output/
- `update-baselines:` writes to screenshot-baselines/screens/, with `git diff --stat` guidance
- `validate-screenshots:` runs capture + compare_baselines.py in one step
- `ci:` includes validate-screenshots in the test gate

Both patterns are proven in production (99 baselines, 0.0000% diff tolerance).
