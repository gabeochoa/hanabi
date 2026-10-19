# N-pane tiling

Reference model: Puffin's `AgentcloudTabModel` — every tab carries a group, groups are
contiguous and numbered 0..n, each group has its own selection, a split inserts a group
(`splitOff`, `reserveGroup`), an emptied group collapses (`normalizeGroups`), pane identity is a
key that outlives renumbering, widths are equal shares with a 480 px floor and the window grows.

Pure pieces, unit-tested on any host:

| file | what |
|---|---|
| `src/ecs/tab_groups.h` | the model: open, preview reuse, select, close, split_off, reserve_group, move_to_group, step, pane_step, kept run, group keys |
| `src/ecs/tab_groups_json.h` | layout to and from JSON, dropping what is gone |
| `src/ecs/pane_layout.h` | shares per pane, drag stopping at the floor, widths past the floor, window extent needed |
| `src/ecs/pane_state.h` | per-pane draft/scroll/latch key now carries the whole pane index |
| `src/settings.*` | `tab_layout` persisted beside `open_tabs` |

Where the app still assumes two panes (inventory at `c3ec6d59`, each is a stage's work):

- `AppComponent::panes` is a vector that starts at two; `splitOpen` and `splitRatio` stand for "more than one pane" and "pane 0's share".
- `active_pane_count()` is `splitOpen ? 2 : 1`; `other_pane()` is `focusedPane == 0 ? 1 : 0`; `pane_index()` is `&pane == &panes[1] ? 1 : 0`; `pane_suffix` indexes `kNames[i & 1]`.
- Per-pane arrays of two: `restoreSplitIds`, `queuedEditing`, `composerRows_`, `composerFocusedLast_`, `composerIdsLast_`.
- `render_split` builds left, divider, right; `pane_screen_rect` computes two rects; `divider_bar` drags one ratio; debug names are `split_left` / `split_right`.
- Settings persists `split_open`, `split_ratio`, `split_focused_pane`, `split_panes[2]`.
- The tab strip is one strip for the window; `reconcile_panes_with_tabs` ties each pane's thread to an open tab; Split shows the same thread in both panes, where the model moves the tab.

Stages, each Mac-verified (build, units, clang 21 TUs, sweep) before the next:

1. Seams that change nothing at two panes: `clamp_pane`, vector panes, pane key, `tab_layout` setting.
2. Layout at N: `pane_screen_rect`, `render_split`, `divider_bar` over `PaneShares`; debug names `split_pane_<i>` from index 2 on; per-pane arrays sized to `kMaxPanes`.
3. A `TabGroups` owned by the tab strip and kept in step with every tab mutation; panes show each group's selection; `splitOpen` becomes `group_count() > 1`.
4. Split Right / Split Down / Move Pane to Next/Previous Pane: menu, palette, chords, tab context menu; fixtures first.
5. Per-group strips; drag a tab between strips.
6. Persist through `tab_layout`, restore with the pinned filter; floor of 480 and window growth.
