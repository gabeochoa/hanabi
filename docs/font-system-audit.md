# Font system audit

## Diagnosis

Hanabi's named type ramp already matches Puffin's role values numerically: message text is 13, input/code is 12, chrome body is 11, headings are 14/16/20. The longstanding mismatch was not scattered role values. It was four lower-level contracts:

1. afterhours/fontstash interprets the requested size as a pixel-height target, while AppKit/CoreText point sizes produce a face-specific typographic line height. At a requested 13, fontstash reported a 13.0 logical-pixel line for every face; native CoreText reported 15.234 for Roboto, 16.120 for Atkinson Hyperlegible, 15.514 for SF Pro Text, and 16.750 for Optimistic.
2. `measure_text_internal` measures the backend-global first-loaded font. Replacing `FontManager["__default"]` changed drawing but did not change this measurement face, so app-owned width caches could remain internally consistent and still measure Roboto after another face was visible.
3. afterhours exposes both pen advance and ink bounds. On the same 13-point-equivalent sample, fontstash's ink box was 1-2 logical pixels wider than advance. Layout and ellipsis need advance; visual-density comparisons need ink. Treating them as interchangeable caused the historical two-pixel drift.
4. Weight rendering is implemented in the pinned afterhours source. The blocker was face registration, not renderer support. No bold face is bundled, and adding font binaries is unnecessary when safe installed faces can be resolved by CoreText.

## Resolution

The app now resolves a bounded allowlist of installed PostScript faces through CoreText: SF Pro Text and Optimistic for family selection, plus installed Roboto and Atkinson variants for emphasis. It never exposes arbitrary fonts. Each face must resolve to a readable, dedicated file path; collection-only aliases are not offered because afterhours accepts a path but no collection face index.

Standard Roboto and Hyperlegible regular remain bundled and deterministic. Headless/test runs ignore installed fonts unless `HANABI_ALLOW_SYSTEM_FONTS_HEADLESS=1` is explicitly set, so ordinary screenshot and UI tests do not depend on host font installation. Missing or invalid family/weight choices resolve to bundled Roboto Regular.

A family carries its CoreText line-height-to-point conversion. The selected scale is applied to both Hanabi's role tokens and afterhours' legacy size tiers, while geometry and `ui_scale` remain unchanged. Transcript prose stays 13 pt; only its fontstash request changes to the face-correct pixel height. Chrome headings and the active tab use the configurable emphasis face; transcript prose remains regular.

Switches alias already-loaded stable font IDs into `__default` and its weighted variants. Repeated toggles do not reload per frame. The maximum reachable set is 14 fontstash faces, below afterhours' 16-face tracking limit. Every effective face or emphasis change increments Hanabi's text epoch and clears afterhours' `TextMeasureCache`; app advance measurement now resolves the selected face explicitly.

## Measurements

Native CoreText at 13 pt, sample `Agentcloud Wg 0123456789`:

| Face | Advance | Typographic line | Point→fontstash scale |
|---|---:|---:|---:|
| Roboto Regular | 164.017 | 15.234 | 1.17185 |
| Atkinson Hyperlegible Regular | 162.591 | 16.120 | 1.24000 |
| SF Pro Text Regular | 175.703 | 15.514 | 1.19336 |
| Optimistic Regular | 179.458 | 16.750 | 1.28850 |

After conversion, fontstash reported:

| Face | Advance | Ink box | Line |
|---|---:|---:|---:|
| Roboto Regular | 159 | 161 | 15.2 |
| SF Pro Text Regular | 177 | 179 | 15.5 |
| SF Pro Text Bold | 188 | 190 | 15.5 |
| Optimistic Regular | 180 | 182 | 16.7 |
| Optimistic Bold | 185 | 186 | 16.7 |

Against `docs/visual-parity/ref/01_home.png` at 1180×949, sidebar structural difference moved from 14.45% with unconverted bundled Roboto to 12.52% with point-correct Optimistic and 12.73% with point-correct SF Pro Text. The region's measured raster-phase floor is 6.17-8.84%. Windowed startup was also exercised through the real Sokol/AppKit path: `dpi=2.00`, regular advance 177.5, bold advance 189.0, and line height 15.5 logical pixels. The matched System Bold 1x/2x headless captures differ in 10.64% of pixels, and the 2x layout-zoom sidebar score worsens to 14.75%, confirming that zoom is not a Retina substitute.

The 70 committed screenshot states were captured from current main before the change and matched at 0.0000%. After the typography change, the full set was recaptured and inspected, then passed 70/70 against the adopted baselines. Dedicated dark/light settings captures exercised installed System + Bold; dedicated captures also covered bundled Standard, bundled Hyperlegible, installed Optimistic, regular/bold, missing-system fallback, and 1x/2x. A live picker run logged epochs 1 → 2 → 3 for Standard/Regular → System/Regular → System/Bold, and the unavailable-System headless render was byte-identical to bundled Standard.
