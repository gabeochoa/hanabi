# Vendored afterhours patches (proven in hanabi, ready for the maintainer)

`vendor/afterhours` is the pinned afterhours submodule
(`428047e3c92442e0ded3a0d473315e9636a451ac`). This directory contains both
older Hanabi-proven fixes and proof patches that are applied only to temporary
vendor copies by `make verify-vendor-patches`. The verifier checks the base,
checks and applies each patch independently, compiles focused probes, and
requires the intended failure before each fix and success after it. The pinned
submodule is never edited.

## Applying (from the afterhours repo/submodule root)

These are `git format-patch`-style patches (they carry From/Date/Subject), so the
PREFERRED path preserves the commit message + authorship:

    cd vendor/afterhours
    git am ../../vendor_patches/<file>.patch          # keeps message + author

If you only want the diff applied to the working tree (no commit), use:

    git apply ../../vendor_patches/<file>.patch        # working-tree only
    git apply --check ../../vendor_patches/<file>.patch # dry-run: verify it applies

Every patch states its pinned base in the commit message. The proof-patch set
below applies independently to `428047e`; the older patches retain their own
bases. After a patch lands upstream, bump Hanabi's submodule pointer and remove
the corresponding patch here.

## Verified proof patches on 428047e

| Patch | Gaps | Vendor delta | Focused proof |
|---|---:|---:|---|
| `351-report-font-atlas-exhaustion.patch` | #351 | +12 | The source contract is absent before and present after; the patched Sokol headers compile as Objective-C++. |
| `210-reject-unsamplable-textures.patch` | #210 | +7 | The sampler validation/cleanup contract is absent before and present after; the patched Sokol headers compile. |
| `265-focus-ring-contrast-toggle.patch` | #265 | +17/-12 | The none backend records three outlines by default in both renderers; disabling contrast records exactly one after the patch. |
| `255-word-editing-capability.patch` | #255 | +7 | A consumer `static_assert` does not compile before; complete and incomplete action enums classify correctly after. |

Run:

    make verify-vendor-patches

Expected runtime is about one minute on Apple Silicon. A pass ends with:

    PASS all 5 vendor patches against 428047e3c92442e0ded3a0d473315e9636a451ac

The probes live in `tests/vendor_probes/`; `scripts/verify_vendor_patches.py`
exports the pinned revision, applies each patch to its own temporary copy, and
deletes those copies on exit. These are maintainer-ready proposals, not claims
of upstream acceptance.

## Candidates deliberately not patched

- **#137:** rejected as pixel-unsafe. The public path returns fontstash pen
  advance while the shared cache/layout path returns ink bounds; Hanabi measured
  a 2px delta on identical strings. A one-line change is possible, but either
  direction moves an existing caller's pixels and needs an upstream semantics
  decision.
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
