# Vendored afterhours patches (proven in hanabi, ready for the maintainer)

`vendor/afterhours` is the afterhours submodule (gabeochoa/afterhours). These are
fixes PROTOTYPED + PROVEN while building hanabi: each was applied to the
submodule, verified to fix the real hanabi symptom (screenshot / behavior), and
captured here as a ready-to-apply patch + rationale so landing it upstream is
low-effort. Each corresponds to a numbered entry in `afterhours_gaps.md`.

Apply a patch from the submodule root:
    cd vendor/afterhours && git apply ../../vendor_patches/<file>.patch

## Landed / proven
- **25-rounded-corner-degenerate-triangle.patch** — `draw_rectangle_rounded`'s
  `emit_corner_arc` emitted a 2-vertex (degenerate) sgl triangle for a 0-radius
  (sharp) corner, rendering as a diagonal slice on any MIXED round/sharp corner
  config. The four edge triangles + center fan already tile a square corner, so
  the sharp branch must emit NOTHING. Fix = `return;`. PROVEN: hanabi tabs now
  render clean rounded-top/square-bottom (top_round()) with no glitch; the
  app-side all_round() workaround was removed. (gap #25)
