# Vendored afterhours patches (proven in hanabi, ready for the maintainer)

`vendor/afterhours` is the afterhours submodule (gabeochoa/afterhours). These are
fixes PROTOTYPED + PROVEN while building hanabi: each was applied to the
submodule, verified to fix the real hanabi symptom (screenshot / behavior), and
captured here as a ready-to-apply patch + rationale so landing it upstream is
low-effort. Each corresponds to a numbered entry in `afterhours_gaps.md`.

## Applying (from the afterhours repo/submodule root)

These are `git format-patch`-style patches (they carry From/Date/Subject), so the
PREFERRED path preserves the commit message + authorship:

    cd vendor/afterhours
    git am ../../vendor_patches/<file>.patch          # keeps message + author

If you only want the diff applied to the working tree (no commit), use:

    git apply ../../vendor_patches/<file>.patch        # working-tree only
    git apply --check ../../vendor_patches/<file>.patch # dry-run: verify it applies

Both are verified to apply cleanly against the pinned base (edfe234). Apply order
is independent — the two patches touch different files (drawing_helpers.h vs
rendering.h) and do not conflict. After landing upstream, bump hanabi's
`vendor/afterhours` submodule pointer to the new commit and delete the applied
patch from this folder.

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

- **30-smooth-eased-scrolling.patch** — scroll was a raw wheel-delta add to the
  rendered `scroll_offset` (no smoothing), so scrolling felt stepped/janky vs
  native macOS momentum scroll. Adds `scroll_target` + `scroll_smoothing`;
  `scroll_offset` eases toward the target each frame (default smoothing=1 =
  legacy instant; 0.28 = smooth glide). Backwards compatible. PROVEN: hanabi
  transcript/sidebar glide smoothly; ease math unit-verified; hanabi builds
  clean against BOTH pinned edfe234 (SFINAE-guarded no-op) and patched. (gap #30)
