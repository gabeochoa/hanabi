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

All 38 baseline PNGs and a fresh 38-state capture from `scripts/screens.sh` were reviewed as one matrix. The highest-signal states are:

- `01_home_dark`, `02_home_light`: full surface stack, navigation hierarchy, section labels, cards.
- `03_transcript_dark`, `04_transcript_light`: titlebar/tab strip against the real content and composer boundaries.
- `06_hover_row_star_dark`, `07_hover_tab_dark`: hover strength and glyph reveal.
- `08_view_blocked_dark`, `09_view_review_dark`, `10b_view_pinned_row_dark`, `11b_view_archived_row_dark`, `12_view_blocked_light`: attention, selection, pinned, settled, and light-theme state semantics.
- `14_sidebar_folded_dark`: rail alignment and hit-target consistency.
- `19_many_tabs_dark`: compressed tab labels and close affordances.
- `22_split_view_dark`: global strip and pane boundaries.
- `23_skeleton_dark`, `24_thread_loading_dark`: frame quality when content is sparse.

## coherent change set

1. Four stable surface roles: window chrome, navigation rail, content canvas, raised control/card.
2. One spacing rhythm: 4 / 8 / 12 / 16 / 24, with 28 px minimum chrome targets and 32 px rows.
3. Four type roles in chrome: 10.5 section, 12.5 tab/meta, 13 navigation/list, 20 page title.
4. One selection rule: a quiet accent-tinted fill; accent remains for focus and active/saved state, while red/green stay semantic.
5. Four row glyph states by both shape and theme color: running/accent, blocked/red, done/green, idle/muted.

## constraints

Transcript message/tool rendering, composer, settings/palette sheets, native packaging, and `vendor/afterhours` remain unchanged. Static styling must not create per-frame allocations or increase steady-state widget counts. Screenshot baselines move only after fresh captures, direct diffs, bounds checks, and dark/light review.

## baseline measurements

- Allocations/frame: home20 `811`, home2000 `1163`, thread480 `2735`, six-line draft `1025`.
- Widget counts: 20 sessions `322`, 2000 sessions `428` (`1.33x`).
- Fresh before capture: 38/38 states produced at 1100×760; settings restored byte-for-byte.
