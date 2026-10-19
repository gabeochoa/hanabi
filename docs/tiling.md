# N-pane tiling

Reference model: Puffin's `AgentcloudTabModel` — every tab carries a group, groups are
contiguous and numbered 0..n, each group has its own selection, a split inserts a group
(`splitOff`, `reserveGroup`), an emptied group collapses (`normalizeGroups`), pane identity is a
key that outlives renumbering, widths are equal shares with a 480 px floor and the window grows.

| file | what |
|---|---|
| `src/ecs/tab_groups.h` | the model: open, preview reuse, select, close, split_off, reserve_group, move_to_group, move_group, merge_all, step, pane_step, kept run, group keys |
| `src/ecs/tab_groups_json.h` | layout to and from JSON, dropping what is gone; a saved mirror split becomes two panes |
| `src/ecs/pane_layout.h` | shares per pane, drag stopping at the floor, widths past the floor, window extent needed |
| `src/ecs/pane_geometry.h` | pane widths and origins, and the half of a pane a drop lands on |
| `src/ecs/tab_model.h` | the app side: groups_of / apply_groups / repair_groups, split_tab, move_tab_to_group, open_in_split, merge_panes, group-aware open / close / bulk close |
| `src/api/draft_key.h` | a draft is keyed by its thread, and saved drafts are re-keyed once |
| `src/settings.*` | `tab_layout` persisted beside `open_tabs` |

## Mirror retired (owner, 2026-10-19: "copy Puffin as much as possible")

A thread is in one pane at a time. Cmd+\ splits the focused tab out into a pane to the right (it
moves; a lone tab does not split), and with more than one pane it merges them back. Where Hanabi
differed from Puffin, and what was done:

| mirror-era behaviour | now |
|---|---|
| Cmd+\ showed the same thread in both panes | Split Right semantics: the tab moves, the moved tab takes the keyboard |
| one global tab strip, a click opened the thread in the focused pane | one strip per pane; a click shows the tab in its own pane |
| Cmd+1…9 and Cmd+[ ] counted the whole strip | count along the focused pane's strip (`session(forSlot:in:)`) |
| Close Others / Close to the Right closed across panes | act on the tab's own pane |
| a tab dropped below the strip split only to the right half of the area | the half of the pane under the pointer decides left or right; a drop on another pane's strip joins that pane |
| Open in Split (sidebar, hook) put a thread in the right pane without a tab | opens a new tab in a new pane beside the focused one; an open thread is just shown |
| tab menu "Open in split" mirrored | Split Right on that tab |
| draft, attachments and scroll state keyed by pane and thread | keyed by thread; a moved tab keeps them, saved drafts re-keyed once (pseudo ids such as the new-task composer stay per pane) |
| saved `split_open` / `split_panes` | read as a layout: the second pane's tab moves to a pane of its own; a split that cannot be two panes restores as one |
| the right pane kept its transcript after the split closed | every pane past the first is released when its pane closes |

Not aligned, with the reason:

- The pane × (merge all) has no Puffin counterpart; kept because fixtures drive it.
- The tab and row menus carry one Split item, Split Right; Puffin has Left / Right / Up / Down in both.
  Four rows take the tab menu from 719 px to 827 px, taller than the 760 px fixture window; the
  directions are reachable by chord, the Window menu and the drag zones (Split Down needs a vertical
  layout, which is not built).
- Split Down sets the axis but panes still lay out in a row.
- The divider is a ratio for two panes and shares for three or more.

Stages, each Mac-verified (build, units, clang 21 TUs, sweep) before the next:

1. Seams that change nothing at two panes: `clamp_pane`, vector panes, pane key, `tab_layout` setting. ✓
2. Layout at N: `pane_screen_rect`, `render_split`, `divider_bar` over shares; per-pane arrays. ✓
3. A `TabGroups` kept in step with every tab mutation; panes show each group's selection. ✓
4. Split Right / Down and Move Pane commands. ✓
5. Per-group strips. ✓
6. Persist through `tab_layout`; floor of 480 and window growth. ✓
7. Mirror retired (this round).
