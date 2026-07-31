# afterhours UI feasibility: collapsible sidebar, tabs, split view

**Verdict: build it all in app code. No upstream additions required.**

The current vendored afterhours immediate-mode UI already exposes every primitive
needed for (1) a collapsible sidebar, (2) VS Code–style closable content tabs, and
(3) draggable split panes. A sibling app (`floatinghotel`) already ships all three,
built purely in its own ECS systems against this same library. Cite-backed details
below.

---

## What afterhours already gives us

Immediate-mode component functions (`vendor/afterhours/src/plugins/ui/imm_components.h`):

- `div`, `hstack`, `vstack`, `spacer`, `tray`, `button`, `button_group`,
  `toggle_button`, `checkbox`, `radio_group`, `toggle_switch`, `slider`,
  `dropdown`, `pagination`, `virtual_list`, `image`, `sprite`, `icon_row`,
  `progress_bar`.
- **`tab_container(ctx, mk(parent), labels, active_tab, cfg)`** — a first-class
  tab-strip primitive (imm_components.h:1769). Horizontal equal-width tabs, active
  highlight + accent underline, click-to-switch, keyboard arrows, mutates
  `active_tab` and returns a truthy `ElementResult` on change. This is exactly a
  tab strip; content is swapped by the caller keyed on `active_tab`.
- `ElementResult` is truthy when a widget was clicked/changed — so click handling
  is `if (button(...)) { ... }`. No event system to wire.

Sizing (`vendor/afterhours/src/plugins/ui/component_config.h`, `layout_types.h`):

- `pixels(n)`, `percent(f)`, `children()`, `expand()`, plus design-space
  `h720(n)`/`w1280(n)` that scale to the current resolution via
  `resolve_to_pixels(...)`. Widths/heights are just data recomputed per frame —
  animating a width is a per-frame value change, nothing special.
- `with_absolute_position()` + `with_translate(x,y)` for free placement.
- `with_overflow(Overflow::Scroll|Hidden|Auto, Axis::X|Y)` per axis; the
  `preset::ScrollPanel()` already used in hanabi wraps `Overflow::Auto` on Y.
- `with_render_layer(int)` — stable z-order (rendering.h:1617 sorts by layer,
  preserving pre-order within a layer). Used for tab strips over content and for
  modal/dialog layers.
- `modal` plugin (`plugins/modal.h`) and `text_input` (`plugins/ui/text_input/`)
  are both vendored and in use by floatinghotel.

## Proof by precedent — floatinghotel (READ-ONLY reference)

Located at `/Users/gabeochoa/projects/floatinghotel/`. It targets the same
afterhours and already implements everything hanabi wants, in app code:

### Tabs — `src/ecs/tab_bar_system.h` (`TabBarSystem`)
- Model: each open tab is an **Entity** with a `Tab` component; the active one has
  an `ActiveTab` marker component; `TabStripComponent.tabOrder` is a
  `std::vector<EntityID>` for ordering.
- Render: iterates `tabOrder`, draws each tab as an absolute-positioned `button()`
  with a per-tab close `button()` ("×"), a "+" add button, dividers — all on
  `render_layer(6/7)`. Click → `switch_to_tab()`; close → `close_tab()` sets
  `entity.cleanup = true`. Cmd+T / Cmd+W handled inline via `is_key_pressed`.
- Per-tab state: `switch_to_tab()`/`create_new_tab()` save the outgoing tab's view
  state into its `Tab` component and load the incoming tab's into
  `LayoutComponent`; `TabSyncSystem` keeps them in sync each frame.
- Content swap: content systems query `find_singleton<RepoComponent, ActiveTab>()`
  so the visible panel is just whatever the active tab points at — pure conditional
  immediate-mode creation.
- (afterhours' own `tab_container()` is a simpler alternative when you don't need
  closable/reorderable tabs.)

### Collapsible sidebar — `src/ecs/sidebar_system.h` + `src/ecs/layout_system.h`
- Toggle: `SidebarSystem::for_each_with` early-returns on `!layout.sidebarVisible`
  — hiding the whole sidebar is one bool. Sub-mode content (Changes/Refs, review
  tabs) is swapped by reading `layout.sidebarMode` and emitting different children.
- Sub-tabs inside the sidebar are done with plain `button()`s whose truthy result
  mutates `layout` (`render_sidebar_mode_tabs`, `render_view_mode_tabs`).
- **Animated collapse** (`LayoutUpdateSystem`, layout_system.h): the sidebar width
  is a fixed logical-pixel value; the *window/content* width is tweened frame by
  frame with a smoothstep ease (`animT += dt/0.18f`), and the layout is recomputed
  at the animated width each frame — the CSS-transition feel, entirely in app code.
  A thin "icon rail when folded / full list when unfolded" is the same pattern:
  compute `sidebar.width` from a `collapsed` bool (small vs full), optionally tween
  it, and emit icon-only vs full rows based on that bool.
- Draggable width: a divider `div` with `HasDragListener`; while `.down`, read the
  mouse and write the new width/ratio back to `LayoutComponent`.

### Split view — `src/ui/split_panel.h` / `split_panel.cpp`
- `split_panel(ctx, parent, id, cfg, totalW, totalH)` returns
  `{firstPane, secondPane, splitPosition}`; `draggable_divider(...)` returns the
  drag delta. The caller persists `splitPosition` across frames. This is a
  self-contained app-code helper — no library primitive needed. floatinghotel also
  splits main content vs command-log via `layout.commandLogVisible` +
  `commandLogHeight` in `LayoutUpdateSystem`.

## Feasibility, point by point

- **Collapsible sidebar — YES, trivially.** Width is a per-frame value; toggling is
  a bool that either early-returns (hidden) or picks small-vs-full width and
  icon-vs-full children. Animation = tween the value with `dt` (floatinghotel does
  exactly this). No upstream need.
- **Tabbed panels — YES.** Either use afterhours' built-in `tab_container()` for a
  simple strip, or floatinghotel's entity-per-tab model for closable/reorderable/
  per-tab-state tabs. Switching the visible panel is conditional immediate-mode
  creation keyed on active-tab state (`ActiveTab` marker or a `size_t active_tab`).
  Close = mark the tab entity `cleanup`. No upstream need.
- **Split view — YES.** A draggable divider `div` (`HasDragListener`) writing a
  split ratio/position into a layout component, with two panes sized from it. Copy
  floatinghotel's `split_panel` helper. No upstream need.

## Known afterhours gaps (from the ecosystem, none blocking)

`~/p/wm_afterhours/AFTERHOURS_GAPS.md` catalogs real issues found in this library.
The only tab-related one — `tab_container` tab strip rendering outside an
absolutely-positioned parent — is already **FIXED** upstream (absolute_pos_x/y in
component_init/autolayout). The rest are cosmetic (focus-ring verification, dropdown
sizing) and don't touch sidebar/tabs/split. Nothing there requires a new primitive
for our three features.

## Recommended architecture for hanabi

Mirror floatinghotel's ECS shape (hanabi already follows the same layout/systems
pattern in `src/ecs/`):

- **Components** (`src/ecs/components.h`):
  - Extend `LayoutComponent` with: `bool sidebarCollapsed`, `float sidebarWidth`
    (full) + `sidebarRailWidth` (folded), and optional anim fields
    (`animFrom/animTarget/animT/animating`) if you want the smooth tween.
  - Add a `Tab` component (thread id/title + per-tab view state) and an `ActiveTab`
    marker; add a `TabStripComponent { std::vector<EntityID> tabOrder; }` singleton.
    Each open thread = one Tab entity.
- **Systems** (register order: data/layout → UI-creating → post-layout, as in
  `main.cpp:build_systems`):
  - `LayoutSystem` (already exists): compute `sidebar` / `transcript` / `statusBar`
    rects from `sidebarCollapsed` (rail vs full width, optionally tweened) and from
    the tab-strip height. Transcript pane x/width shift with the sidebar width.
  - `TabBarSystem`: render the tab strip (afterhours `tab_container()` for MVP, or
    the entity-per-tab model for closable tabs) above the transcript on a higher
    `render_layer`; on switch, point the transcript at the active tab's thread.
  - `SidebarSystem` (rename/extend the current session list): if collapsed, emit an
    icon rail (`icon_row`/small `button`s); else the full session list. A collapse
    toggle button flips `sidebarCollapsed`.
  - `TranscriptSystem` (already exists): unchanged except it reads the active tab's
    thread instead of a single `openSession`.
  - Later: a `split_panel` helper (copy floatinghotel's) if a second transcript
    pane is wanted.
- **Interaction:** all click handling stays inline (`if (button(...)) mutate
  state;`). No new event/plumbing layer.

Bottom line: ship it in app code; do not file an afterhours request for panel/tab
window support.
