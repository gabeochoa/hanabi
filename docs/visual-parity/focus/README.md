# Focus ring: what was wrong, in pixels

Captured with `output/hanabi_uitest.exe --e2e`, mock backend, 1180x949, dark
theme, two pinned tabs — the same fixture as the parity shots. The scripted
harness cannot see a colour or an outline (afterhours_gaps.md #61), so these
are the evidence for every claim in the `fix/focus-rings` commits that is
about how the ring LOOKS. Enlarged with nearest-neighbour; no other processing.

| file | before (top) | after (bottom) |
| --- | --- | --- |
| `01_ring_before_after.png` | the "1px hairline" as three lines, white-blue-white, corners splayed into brackets | one line of {173,192,255}, contrast edges at {7,7,10} against a {23,23,35} backdrop, square corners |
| `02_chip_before_after.png` | the composer's effort chip wearing a pill-shaped ring — theme roundness 0.5 on an 18px transparent button | a rectangle, and the three outlines exactly concentric |
| `03_arrow_double_ring.png` | pressing Left to move the caret one character adds a second outline around a field that already draws its own focused border | (the arrow no longer arms the ring at all) |
| `04_tab_walk.png` | — | five frames: Tab walking Home, Settings, Blocked, Review, Pinned. Before the WidgetNext binding all five were the same frame |

The numbers behind them are in the commit messages and in gaps #265-#267.
