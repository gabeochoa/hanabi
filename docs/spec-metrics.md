# hanabi — pixel spec (source of truth for pixel-perfect validation)

Extracted from mock/index.html. The C++ build must MATCH these. Validation
agents compare screenshots against these numbers + the corresponding split
mock (docs/mock-phases/phaseN.html).

## Window / chrome
- App window: 1180 x 760 (content); resizable.
- macOS system menu bar (outside window): height 26px, font 13px.
- App title bar: height 38px; left traffic lights (3 × 12px dots, 8px gap);
  centered title "hanabi" 12.5px (bold on the name).
- Status bar (bottom): height 26px; left "backend: mock" + blocked-count,
  right "N sessions"; font 11px.

## Sidebar
- Unfolded width: 280px. Folded rail width: 52px. Width animates ~0.18s (smoothstep).
- Header row: min-height 40px, padding 9px 8px 5px 14px. Brand "✦ hanabi":
  mark 15px accent, name 13.5px/700. Action iconbtns 26x26, radius 7, icons 16px.
- Search: under header, padding 10/10/6; field radius 8, padding 5x8, icon 13px, input 12.5px.
- Section label ("Views"/"Folders"): 10.5px, uppercase, weight 600, color faint, padding 10/14/5.
- Smart item: padding 6x8, radius 8, gap 10; icon box 18px (svg 16px); label 13px/500;
  count 11px tabular faint. Active row uses selected-bg. Folded: centered, 8px vert pad.
- Folder head: padding 6x8, radius 7, gap 6; chevron 12px; folder icon 13px; name 12px/600; count 11px.
- Folder body: indented (padding-left 14px, margin-left 13px, 1px left border guide).

## Thread rows (dense, high-signal)
- Chat row: padding 3px 8px 3px 10px, radius 6, gap 8, line-height 1.25.
- STATUS GLYPH (shape+color, ~9px, left of title), shown only when attention:
  * blocked/needs-you = RED up-triangle (base 10px, height 9px).
  * review = GREEN diamond (8px square rotated 45°).
  * done = BLUE dot (8px circle).
  * working/parked/archived = NO status glyph.
- Title (.ctitle): 12.5px; attention rows bold + primary color; running rows faint;
  parked/archived 42% opacity, faint.
- NO text tag chip in sidebar rows (glyph only). Chips remain on smart-view cards.
- SUB-AGENTS: collapsed by default. Disclosure chevron (11px) left of the row when
  subs exist; sub count (10px) at right. HOLLOW working RING (9px, 1.6px outline,
  accent, pulsing) sits just RIGHT of the parent status shape when any sub is running.
  Sub-rows: indent 22px, twig 8px, title 11.5px faint; child glyph 8px (ring/dot/triangle).

## Tabs (main pane top)
- Tab strip: height 38px, bottom 1px border, bg = sidebar-bg.
- Tab: padding 0 12px, gap 8, font 12.5px, max-width 220px, right 1px border.
  Active tab: bg = window bg, primary text, accent underline. Close × svg 10px.

## Transcript
- Header h2 14px/700; sub 11.5px secondary.
- Message role label + timestamp small; body wraps; role accent colors:
  user rgb(90,128,255), assistant rgb(126,200,140), system rgb(180,150,90), tool rgb(150,130,200).
- Composer pinned bottom: textarea + send button.

## Type scale (allowed font sizes — do not invent others)
9, 9.5, 10, 10.5, 11, 11.5, 12, 12.5, 13, 13.5, 14, 15, 17(spotlight), 20(h1).

## Color tokens (dark defaults; light variant exists)
- bg rgb(28,28,32); sidebar-bg rgb(22,22,26); panel-bg rgb(28,28,32); border rgb(52,52,60).
- text-primary rgb(222,222,228); text-secondary rgb(138,138,150); text-faint ~rgb(110,110,122).
- accent rgb(90,128,255); selected-bg rgb(48,66,120); hover-bg rgb(40,40,48).
- tag-blocked-fg rgb(255,120,120); tag-ready/review-fg rgb(126,210,150); tag-done-fg rgb(120,160,255).
