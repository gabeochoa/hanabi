# UI chrome audit

## ranked findings

1. **The app has no depth model in dark mode.** `window_bg`, `sidebar_bg`, and `panel_bg` are all `#171723`, so the titlebar, navigation rail, and work surface collapse into one plane. The only large contrast jump is the blue selection fill, which makes selection feel louder than the frame around it.
2. **Navigation consumes too much attention and too much height.** At 760 px tall, 296 px of fixed sidebar chrome appears before the conversation list. The six 32 px view rows use 16.8 px labels while conversation titles use 16.5 px and tabs use 12.5 px, leaving almost no type hierarchy between navigation and content.
3. **State styling is not actually theme-aware.** Sidebar labels, view icons, count badges, session titles, and the four row-state glyphs use dark-reference constants. Light mode changes the planes but keeps those dark-tuned inks and badge fills, so it reads as a converted dark theme rather than a designed light theme.
4. **The tab strip spends 67 px to show a 34 px tab row.** Inactive tabs each carry an outline even though the strip already has a bottom rule, producing repeated boxes across the titlebar. The active tab, sidebar selection, focus ring, pinned star, and attention markers also use competing selection rules.
5. **The sidebar has too many separators and containers.** A filled `VIEWS` strip, a rule below the view stack, a separately filled search pill, an external filter button, full-width hover bands, ringed count badges, and a footer rule divide a 280 px rail into many small boxes.
6. **The search row is visually assembled rather than composed.** The field and filter are separate controls with different resting surfaces and no shared boundary. The placeholder and filter ink are fixed dark-theme values.
7. **The four row states have a good shape vocabulary but inconsistent color semantics.** Running arc, blocked mark, done check, and idle dot are distinct by shape, but their colors bypass the theme. A pinned star uses the success green, making “saved” look like “completed.”
8. **Main-pane section hierarchy is close but disconnected from chrome rhythm.** The 20 px smart-view title, 10.5 px tracked section label, raised cards, and 24 px content inset are individually reasonable; the frame around them is what makes the page feel pieced together.

## state review

All 68 states declared and baselined on current main `4051207` were reviewed as one matrix. The two narrow-width states from this stack remain additive, bringing the merged matrix to 70. The highest-signal states are:

- `01_home_dark`, `02_home_light`: full surface stack, navigation hierarchy, section labels, cards.
- `03_transcript_dark`, `04_transcript_light`: titlebar/tab strip against the real content and composer boundaries.
- `06_hover_row_star_dark`, `07_hover_tab_dark`: hover strength and glyph reveal.
- `08_view_blocked_dark`, `09_view_review_dark`, `10b_view_pinned_row_dark`, `11b_view_archived_row_dark`, `12_view_blocked_light`: attention, selection, pinned, settled, and light-theme state semantics.
- `13a_chat_welcome_light`, `13c_empty_transcript_light`, and `18a`–`18w`: all landed secondary surfaces in dark/light, empty, menu, picker, toast, auth-error, and transcript-error states.
- `14_sidebar_folded_dark`: rail alignment and hit-target consistency.
- `18p2_tab_menu_dark`, `18q2_tab_menu_light`, `19_many_tabs_dark`: context menu, pin, overflow, and compressed tab visual states.
- `22_split_view_dark`: global strip and pane boundaries.
- `23_skeleton_dark`, `24_thread_loading_dark`: frame quality when content is sparse.
- `33_narrow_dark`, `34_narrow_many_tabs_dark`: frame and overflow behavior at 760×620.

## coherent change set

1. Four stable surface roles: window chrome, navigation rail, content canvas, raised control/card.
2. One spacing rhythm: 4 / 8 / 12 / 16 / 24, with 28 px minimum chrome targets and 32 px rows.
3. Four type roles in chrome: 10.5 section, 12.5 tab/meta, 13 navigation/list, 20 page title.
4. One selection rule: a quiet accent-tinted fill; accent remains for focus and active/saved state, while red/green stay semantic.
5. Four row glyph states by both shape and theme color: running/accent, blocked/red, done/green, idle/muted.

## constraints

Transcript message/tool rendering, composer, settings/palette sheets, native packaging, and `vendor/afterhours` remain unchanged. Static styling must not create per-frame allocations or increase steady-state widget counts. Screenshot baselines move only after fresh captures, direct diffs, bounds checks, and dark/light review.

## baseline measurements

- Current-main allocations/frame: home20 `811`, home2000 `1163`, tabs20 `769`, thread480 `2737`, six-line draft `1025`.
- Current-main widget counts: 20 sessions `322`, 2000 sessions `428` (`1.33x`); event transcript `331/503` widgets and `2373/2353` allocations/frame.
- Fresh before capture: all 68 current-main states plus 2 manually matched narrow states; settings restored byte-for-byte.

## outcome

- The frame now has four ordered surfaces in both themes: titlebar, navigation rail, content canvas, and raised controls/cards. Content-owned palette tokens did not move.
- Navigation and conversation rows now share a 13 px text role, while tabs remain 12.5 px and page titles 20 px.
- The `VIEWS` strip and duplicate rule no longer divide the sidebar into stacked boxes. Search and filtering share one field.
- Active navigation and tabs use the same accent-derived selected fill. Pinned uses accent; blocked and ready/done retain semantic red and green.
- All 70 merged states match their accepted baselines exactly. Every state has `ui-chrome-<state>-before.png` and `ui-chrome-<state>-after.png` in this directory.
- The current-main tab implementation remains authoritative for reorder, pinning, context menus, overflow scroll, active reveal, pane reconciliation, futures, persistence, and geometry. The chrome stack changes tab surfaces, borders, and hover composition only.
- The landed `hanabi::surface` helpers and the color/geometry tokens they consume are byte-identical to current main. All 28 new dark/light secondary states remain covered.
- Locked full suite: 42/42 unit/e2e binaries and 140/140 scripted UI tests. UI isolation passed 140/140; shuffle seed `4051207` passed 140/140; tab persistence and all source/gap checks passed.
- Screenshot validation: `70/70` at `0.0000%`; two `01_home_dark` determinism captures were byte-identical. Bounds: `2 known, 0 new`.
- Against current main, allocations/frame are unchanged: home20 `811`, home2000 `1163`, tabs20 `769`, thread480 `2737`, draft6 `1025`. Catalog widgets improve `322/428 → 321/427`; event widgets improve `331/503 → 330/502`; event allocations remain `2373/2353`.
- The merged gap ledger has 240 numeric headings and 231 distinct numbers; the reference checker passes all 240 entries. The index snapshot remains current-main exact and reserved range #495–#509 remains unused.
- Puffin parity intentionally moves away from pixel similarity: shared structural difference `6.93% → 11.50%`, sidebar `9.49% → 14.45%`, views `4.22% → 15.49%`, list `12.35% → 14.92%`, and tabbar `2.81% → 14.16%`; main remains `3.31%`. This is the cost of the coherent Hanabi surface and selection model, not lost tab behavior.

## left behind

- The titlebar remains 67 px tall because changing its vertical origin also moves transcript and composer geometry owned by other work. See `ui-chrome-03_transcript_dark-after.png` and `ui-chrome-19_many_tabs_dark-after.png`.
- The bounds baseline still records two 1–1.5 px vertical label-box overflows (`sv_icon`, `sv_label`) with no visible clipping. The search-field overflow was removed. See `ui-chrome-12_view_blocked_light-after.png`.
- The renderer still imposes its private 5 px label inset and hard-edged primitive rasterization; these remain existing afterhours gaps #75/#84 and #92. No new afterhours finding was opened, so reserved range #495–#509 remains unused.
