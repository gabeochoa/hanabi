# hanabi — phased build plan + per-phase screenshot validation

Principle: build UP piece by piece. Each phase ends with a SCREENSHOT VALIDATION
GATE. A validation agent compares the phase's C++ screenshot against BOTH
(a) the split reference mock for that phase (docs/mock-phases/phaseN.html) and
(b) the pixel spec (docs/spec-metrics.md). We do NOT advance to phase N+1 until
phase N's UI matches — so bad layout can't accumulate.

Global validation rules (apply EVERY phase — the agent must check all):
- Window is 1180x760; no element overflows the window or its panel bounds.
- FONT SIZES match the spec type scale — flag any text that looks larger/smaller
  than the mock's equivalent element (title 12.5, smart label 13, row title 12.5,
  section label 10.5, counts 11, transcript h2 14). No invented sizes.
- PANEL LAYOUT matches: sidebar 280px (52px folded), title bar 38px, tab strip 38px,
  status bar 26px, transcript fills the rest. Panels aligned, no gaps/overlaps.
- Spacing/padding visually matches the mock (row density, header paddings).
- Colors match tokens (dark default): bg 28/28/32, sidebar 22/22/26, accent 90/128/255.
- Alignment: glyphs, chevrons, counts vertically centered in rows; columns aligned.
- Compare SIDE BY SIDE with docs/mock-phases/phaseN.html rendered at 1180x760 —
  call out ANY pixel-level difference in position, size, weight, or color.
- Verdict: PASS only if it matches the mock; else list each mismatch + a fix.

---

## Phase A — App shell + panels (no data)
Build: window chrome (title bar w/ traffic lights + centered title), empty sidebar
(280px) with header (brand ✦ hanabi + New task + Settings + collapse), empty main
pane with a tab strip, status bar. Dark theme only.
Validate against docs/mock-phases/phaseA.html:
- [ ] Title bar 38px tall; 3 traffic-light dots 12px at far left, 8px apart; "hanabi" centered, 12.5px, name bold.
- [ ] Sidebar exactly 280px wide, bg rgb(22,22,26), 1px right border rgb(52,52,60).
- [ ] Header row ~40px; "✦ hanabi" left (mark 15px accent, name 13.5px bold); three 26px icon buttons right; collapse chevron rightmost.
- [ ] Status bar 26px tall at bottom, full width, 11px text.
- [ ] Main pane bg rgb(28,28,32); tab strip 38px with 1px bottom border.
- [ ] Nothing overflows; panels flush; no stray padding.

## Phase B — Collapsible sidebar (fold/unfold)
Build: collapse toggle + Cmd+B; folded 52px rail (icon-only smart views + collapse),
unfolded 280px full. Animated width.
Validate against docs/mock-phases/phaseB.html (capture BOTH states):
- [ ] Folded rail is 52px; only icons show (no text labels/counts); icons centered.
- [ ] Unfolded is 280px; brand text + search + labels visible again.
- [ ] Collapse toggle present in header both states; chevron flips direction folded vs unfolded.
- [ ] Smart-view icons identical position/size between states (16px svg in 18px box).
- [ ] Search field hidden in folded rail; visible unfolded (radius 8, 12.5px input).

## Phase C — Sidebar content: smart views + folders + rows (mock data)
Build: smart-view list (Home/Blocked/Review/Starred + counts), folders (Stars/Oncall/
Experiments/Recent) + Archived (collapsed), high-signal thread rows with SHAPE glyphs.
Validate against docs/mock-phases/phaseC.html:
- [ ] Smart items: label 13px/500, count 11px right-aligned tabular; active row selected-bg.
- [ ] Section labels "Views"/"Folders" 10.5px uppercase faint.
- [ ] Folder rows: chevron 12px + name 12px/600 + count 11px; body indented with 1px left guide.
- [ ] Thread rows dense (padding 3/8/3/10, ~1.25 line-height); title 12.5px.
- [ ] STATUS GLYPH shapes correct: blocked=RED up-triangle, review=GREEN diamond, done=BLUE dot; running/parked/archived = NO glyph.
- [ ] Attention rows bold+primary; running dimmed; parked/archived ~42% opacity.
- [ ] NO text tag chips in sidebar rows.
- [ ] Archived section collapsed by default, low-signal (plain count, no attention badge).

## Phase D — Sub-agents (nested, collapsed) + working ring
Build: threads with sub-agents show a disclosure chevron (collapsed default) + count;
hollow working ring right of the parent status shape when a sub runs; sub-rows on expand.
Validate against docs/mock-phases/phaseD.html:
- [ ] Sub-agent rows HIDDEN by default; chevron rotates on expand.
- [ ] Hollow working RING (outline, accent, ~9px) appears just right of the parent's filled status shape only when a sub is running.
- [ ] Expanded sub-rows indented 22px with twig connector; title 11.5px faint; child glyph 8px.
- [ ] Ring visually distinct from filled shapes (outline vs solid).

## Phase E — Tabs + transcript
Build: click row -> opens/focuses a tab; tab strip (active underline, × close, Cmd+W);
transcript with role-colored message bubbles, header, composer.
Validate against docs/mock-phases/phaseE.html:
- [ ] Tabs: 12.5px, padding 0 12px, max-width 220px, right 1px border; active tab bg=window bg + accent underline; × is 10px.
- [ ] Transcript header h2 14px/700 + sub 11.5px.
- [ ] Message role accent colors match tokens (user blue / assistant green / system amber / tool purple).
- [ ] Body text wraps inside the bubble (no right-edge clipping).
- [ ] Composer pinned at bottom (textarea + send button).

## Phase F — Smart-view screens (Home digest / Blocked / Review / Starred)
Build: selecting a smart view swaps the main pane to its screen; Home digest layout;
dotted-grid canvas background; light/dark toggle in Settings.
Validate against docs/mock-phases/phaseF.html:
- [ ] Home: "Waiting on you" numbered, then "Pinned to digest", then "Finished", then dimmed "Self-running (N)".
- [ ] Dotted-grid background on smart-view screens (22px grid).
- [ ] Blocked/Review/Starred cards match mock (card padding, chip on cards OK here).
- [ ] Light theme: toggle in Settings flips tokens; both themes match their mock variant.
- [ ] h1 20px; card names 13px; body 12–12.5px.

## Phase G — Native integrations (deferred; own validation later)
menu-bar NSStatusItem (blocked count + new chat), real Spotlight kickoff, global hotkey,
native notifications, offline cache. Validated per-item when built.
