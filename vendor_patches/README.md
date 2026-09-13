# Vendored afterhours patches (proven in hanabi, ready for the maintainer)

`vendor/afterhours` is the pinned afterhours submodule
(`1ac6db21da8768af6bc27248fb6f9484e810a614`). This directory contains both
older Hanabi-proven fixes and proof patches that are applied only to temporary
vendor copies by `make verify-vendor-patches`. The verifier checks the base,
checks and applies each patch independently, compiles focused probes, and
requires the intended failure before each fix and success after it. The pinned
submodule is never edited.

**Nothing applies these patches to the shipping binary.** hanabi compiles the
submodule verbatim, so a green `make verify-vendor-patches` says the patch is a
good upstream contribution and that the PIN STILL LACKS the behaviour — it does
not say hanabi has the fix. Where hanabi needs the behaviour it carries its own
stand-in (`src/util/atlas_guard.h` for #350/#353, `theme::ask_action_disabled_ink()`
for #266), and the verifier asserts those stand-ins are still present. If a
patch lands upstream, bump the pin, delete the patch from here and from
`PATCHES`, and retire the stand-in with it.

## Applying (from the afterhours repo/submodule root)

These are `git format-patch`-style patches (they carry From/Date/Subject), so the
PREFERRED path preserves the commit message + authorship:

    cd vendor/afterhours
    git am ../../vendor_patches/<file>.patch          # keeps message + author

If you only want the diff applied to the working tree (no commit), use:

    git apply ../../vendor_patches/<file>.patch        # working-tree only
    git apply --check ../../vendor_patches/<file>.patch # dry-run: verify it applies

Every patch states its pinned base in the commit message. The proof-patch set
below applies independently to `1ac6db2`; the older patches retain their own
bases. After a patch lands upstream, bump Hanabi's submodule pointer and remove
the corresponding patch here.

## Verified proof patches on 1ac6db2

`PATCHES` in `scripts/verify_vendor_patches.py` covers the two below. The other
four files in this directory (`22`, `25`, `30`, `305`) are **not** in `PATCHES`
and are verified by nothing.

`210-reject-unsamplable-textures.patch` and `255-word-editing-capability.patch`
are **gone: both landed upstream** in the 9ff9079..1ac6db2 range -- `865c4e6`
checks `sg_query_sampler_state` and adds `AFTERHOURS_SG_SAMPLER_POOL_SIZE`;
`7208d0c` exposes `text_input::has_editing_action` and reports the missing names
once per run. The 210 patch no longer applied (`git apply --check` failed at
`drawing_helpers.h:1412`); the 255 patch still applied but its trait was its own
name, so its probe could not tell the pin's fix from the absence. hanabi's
`src/preload.cpp` now `static_assert`s its four names through the upstream
trait, which is the live proof.

`351-report-font-atlas-exhaustion.patch` was here and is **gone: it landed
upstream** as `bdea3b9` "Say when the font atlas is full, and let it be
resized", which registers `fonsSetErrorCallback` and adds
`AFTERHOURS_FONT_ATLAS_SIZE`. The verifier's own `require_red` would have
failed on it at this pin, which is the check working as designed.
`src/util/atlas_guard.h` STAYS: #350 (ask whether a measurement dropped a
glyph) and #353 (draw the substitute glyph) are still open, so the probe is
still the only way hanabi can know a measurement is wrong rather than merely
that the atlas filled.

| Patch | Gaps | Vendor delta | Focused proof |
|---|---:|---:|---|
| `265-focus-ring-contrast-toggle.patch` | #265 | +17/-12 | The none backend records three outlines by default in both renderers; disabling contrast records exactly one after the patch. |
| `266-explicit-disabled-label-color.patch` | #266 | +9 | The disabled-label probe drives afterhours' own harness and reads back the drawn colour: implicit before, the explicit disabled colour after. |

Run:

    make verify-vendor-patches

Expected runtime is about one minute on Apple Silicon. A pass ends with:

    PASS all 2 vendor patches are absent from the pinned tree
    1ac6db21da8768af6bc27248fb6f9484e810a614 and apply cleanly to it. The app
    builds against the UNPATCHED pin; hanabi's own stand-ins are what ship.

The probes live in `tests/vendor_probes/`; `scripts/verify_vendor_patches.py`
exports the pinned revision, applies each patch to its own temporary copy, and
deletes those copies on exit. These are maintainer-ready proposals, not claims
of upstream acceptance.

## Candidates deliberately not patched

- **#137:** CLOSED upstream at 1ac6db2 (`82145f9` measures by advance). The 2px
  the rejected one-liner would have moved is exactly what moved: every centred
  label shifted 1px and the narrow approval card packed one more glyph per row,
  re-baselined in the pin-bump commit.
- **#85/#277:** rejected as not small or differential-safe. The 5px contract is
  duplicated across plain, wrapped, styled, ellipsis, immediate, batched, and
  text-input paths. Honoring element padding would move nine live Hanabi labels,
  while merely naming one literal would leave the other 5px/10px calculations
  divergent.
- **#210 pool sizing:** deferred while shipping the correctness half. Pool sizes
  cross the public graphics config, both Sokol setup paths, defaults, and backend
  portability. Sampler validation is independent and prevents a successful
  return from containing an unusable sampler now.

## Landed / proven
- **25-rounded-corner-degenerate-triangle.patch** — `draw_rectangle_rounded`'s
  `emit_corner_arc` emitted a 2-vertex (degenerate) sgl triangle for a 0-radius
  (sharp) corner, rendering as a diagonal slice on any MIXED round/sharp corner
  config. The four edge triangles + center fan already tile a square corner, so
  the sharp branch must emit NOTHING. Fix = `return;`. PROVEN: hanabi tabs now
  render clean rounded-top/square-bottom (top_round()) with no glitch; the
  app-side all_round() workaround was removed. (gap #25)

- **22-styled-spans-word-wrap.patch** — the multi-color TextSpan label path
  drew all runs on ONE line, so styled labels couldn't be used for a wrapping
  paragraph. Rewrote the span loop to word-wrap across label_rect.width (words
  flow + wrap, each keeps its span color, per-line alignment preserved). Words
  wrap on the SAME boundaries/widths as the plain wrapper, so a caller's line-
  count height model matches. PROVEN: patch applies + hanabi builds + tests 8/8;
  visually confirmed the span path compiles into the buffered render. Submodule
  tag hanabi-fix-gap22. Unblocks inline code pills / bold-italic runs in the
  transcript (gap #22).

  ## Turnkey app-side wiring for hanabi once this lands + the pointer bumps
  (the scaffolding was prototyped + reverted to keep main building against the
  PINNED afterhours; re-apply when vendor is updated):
  1. transcript_render_cache.h `MsgRender`: add `std::string raw;` (redacted +
     bold/list-normalized text with inline-code backticks KEPT; visible width ==
     `body`, so height/wrap unchanged).
  2. main_pane_system.h `measured()`: set `r.raw = strip_inline_md_keep_code(redacted);`
     (a variant of strip_inline_md that keeps backticks — prototype: strips
     **bold**/__ + normalize_md_lines, leaves `code`).
  3. Add `md_spans(line, base, codeCol)` → splits a line into
     std::vector<afterhours::ui::TextSpan> at `code` runs (backticks dropped
     from span text so width matches), empty if no inline code.
  4. render_rich_body: thread `raw` alongside `shown`; in the plain-line branch,
     if `md_spans(rawLine,...)` is non-empty use `.with_styled_label(spans)`
     (+ a subtle code bg via a per-run pill if desired) instead of `.with_label`.
     Height already matches (spans wrap == plain wrap). Same for bold/italic if
     TextSpan grows a weight/style field later.

- **305-text-area-wraps-every-frame.patch** — `text_area` called
  `state.layout_cache.rebuild(...)` unconditionally on every frame, and reached
  past `TextMeasureCache` to the raw backend `measure_text` (building a
  `std::string` per probe to get a `const char*`). `HasTextAreaState` already
  had `needs_layout_rebuild()` and nothing ever called it. Adds a LayoutKey
  holding everything `rebuild` reads (text, wrap width, line height, font size,
  font name) and skips the wrap when none moved; switches the probe to
  `measure_text_line`. PROVEN: hanabi's composer standing still, operator new
  per frame — empty 811→810, one 130-char line 1007→824, six lines 1030→847;
  `make test` green with it applied, and the app builds and behaves identically
  against BOTH the pinned 428047e and the patched tree. Applies cleanly to
  428047e. (gap #305)

  When this lands and the submodule pointer moves, drop
  `scripts/alloc_gate.sh`'s `CEIL_DRAFT6` from 1250 to ~1050 in the same
  commit — the ceiling exists to hold the pinned vendor's number, and the
  comment above it says so.

- **30-smooth-eased-scrolling.patch** — scroll was a raw wheel-delta add to the
  rendered `scroll_offset` (no smoothing), so scrolling felt stepped/janky vs
  native macOS momentum scroll. Adds `scroll_target` + `scroll_smoothing`;
  `scroll_offset` eases toward the target each frame (default smoothing=1 =
  legacy instant; 0.28 = smooth glide). Backwards compatible. PROVEN: hanabi
  transcript/sidebar glide smoothly; ease math unit-verified; hanabi builds
  clean against BOTH pinned edfe234 (SFINAE-guarded no-op) and patched. (gap #30)
