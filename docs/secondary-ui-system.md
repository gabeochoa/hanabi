# Secondary UI system

## Scope

This pass covers settings, new-task and rename sheets, command and full-search panels, device authorization, find, context menus, toasts, model/effort/slash menus, welcome, and empty/error states. It does not redesign sidebar or tab chrome, normal transcript rows, or tool rows.

## Defects ranked by visual impact

1. Settings had the strongest inconsistency: duplicated height accounting, no subtitle hierarchy, dense segmented rows, and inert “Coming soon” sections.
2. Command and full-search panels looked like enlarged text fields rather than product surfaces; selected rows, field treatment, padding, and empty copy diverged.
3. Device authorization presented its URL and code as loose text, with no numbered task flow or distinct failure treatment.
4. Model, effort, slash, row, and tab menus each used different widths, row heights, corners, selection fills, and labels.
5. New-task and rename sheets did not share geometry or title/subtitle roles, and new-task did not place initial keyboard focus in its field.
6. Toast width was fixed, leaving short messages cramped and long messages vulnerable to collision with actions.
7. Welcome and transcript-error states lacked the same hierarchy and surface treatment as the overlays around them.
8. Short windows relied on fixed dimensions rather than a shared viewport clamp.

## System

`src/ui/secondary_surface_geometry.h` owns viewport clamping and bounded toast width. `src/ui/secondary_surface.h` owns sheet, menu, field, option-row, action-button, corner, spacing, and destructive-surface recipes.

Sheets use a title plus explanatory subtitle, 24 px horizontal padding, 20 px vertical padding, a 10 px radius, and a one-pixel theme border. Menus use 36 px rows, 8 px corners, the panel surface, selected fill, and a consistent hover treatment. Fields use a 38 px recessed surface with a visible border. Primary and secondary actions share height and corner treatment. Destructive menu actions and errors use the destructive token on a contrast-checked tinted surface.

Settings now contains only working controls or truthful read-only status. Model and effort remain owned by their live composer pickers; `/model` and `/effort` open those pickers instead of producing dead “no picker yet” messages.

## Interaction and performance

- New-task, command-palette, and full-search fields take focus when opened.
- Escape has one owner for row and tab context menus as well as the existing sheets and pickers. Device authorization deliberately ignores Escape and requires the explicit offline action.
- Settings, shortcuts, authorization, command palette, and full search clamp to a 24 px viewport margin; scrollable content remains reachable at short heights.
- Closed model and effort pickers return before constructing their popover trees after a one-frame state reset. `scripts/alloc_gate.sh` remains at 811 / 1163 / 2735 / 1025 allocations per frame for its four arms.

## Evidence

The deterministic suite now contains 68 baselines. Every secondary surface added in this pass has dark and light coverage, including no-result and failure states. `docs/screenshots/review/secondary-surfaces-before-after.png` contains matched before/after pairs for all 36 changed dark/light states.

## Remaining limits

- The export directory picker is the native macOS `NSOpenPanel`. Its invocation, selected path, persistence, and export destination are tested, but the OS-owned panel itself is not available in the headless widget tree and is not screenshot-baselined.
- The toolkit has no native accessibility name/role bridge, so tests can prove focus, activation, visible labels, and Escape behavior, but not VoiceOver semantics. See gaps #527 and #112/#458.
- Synthetic right-click does not reach direct platform-button polling. The tab menu therefore uses a deterministic render hook for screenshots and Escape tests; native invocation remains a manual check. See gap #526.
