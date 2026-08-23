# Search, Settings & Shortcuts — Implementation Breakdown

**Date:** August 23, 2026  
**Areas:** Search & Find (5 gaps) · Settings & Preferences (8 gaps) · Keyboard & Shortcuts (5 gaps)

---

## Verification Summary

**Gaps verified: 16 actual gaps** (out of 18 from puffin_gaps.md)

**Already built (dropped from scope):**
- Find-in-transcript (Cmd+F) — shipped with 3 e2e tests (`tests/ui/find_*.e2e`)
- Shortcuts reference sheet (Cmd+/) — live in `src/ecs/shortcuts_system.h`
- Settings overlay (Cmd+,) — live in `src/ecs/settings_system.h` with 14 persisted controls
- Sidebar search (local session title + note search) — live in `src/ecs/sidebar_system.h`

**True gaps needing implementation: 16**
- Search: 3 (operators, full-text session search, command palette, snippet highlighting)
- Settings: 6 (send key config, timestamps toggle, typeface picker, text weights, theme rotate, custom theme editor)
- Keyboard: 5 (global hotkey — but see #4a, the mechanism exists; shortcut
  recorder; composer history — PLANNED IN `composer.md`, build it from there;
  navigation shortcuts; find next/prev)

---

## Search & Find (5 gaps → 3 real gaps)

### ✓ SKIP: Gap #1 — Cmd+F find-in-transcript (BUILT)
- Already shipped (`src/ui/find_highlight.h`, `main_pane_system.h`)
- 3 e2e tests pass
- What's missing vs puffin: "Elsewhere" indicator (show matches in unloaded history)
  - This is a size-small enhancement, not core

### Gap #2a: Find operators (is:thinking, has:tool, state:, etc.) — Size: Small
**What ships:**
- Operators parse from the find bar: `python is:tool`, `error state:running`, etc.
- Filtering logic evaluates which rows match the operator
- Syntax errors show as a hint ("Try: is:thinking, has:tool, etc.")

**UX flow:**
1. Cmd+F opens find bar (exists)
2. Type query with operators: `python is:tool`
3. Finds only tool rows containing "python"
4. Cmd+G/Cmd+Shift+G navigate (existing)
5. Escape closes (existing)
6. No matches: "No results"

**Files:**
- `src/ui/find_highlight.h` — add operator parser + evaluation logic (~50 lines)
- `src/ecs/main_pane_system.h` — wire operator results into highlight pipeline (~30 lines)

**Dependencies:**
- None; uses existing find bar

**Testing:**
- E2E: `tests/ui/find_operators.e2e`
  - Can't test Cmd+F via script; instead `# env:` set `showFindBar` and `findQuery` in AppComponent, assert row paint
  - Operators tested: `is:thinking`, `has:tool`, `state:running`, mixed plain-text + operator

---

### Gap #2b: Session search (Cmd+Shift+F) — Size: Medium
**What ships:**
- Cmd+Shift+F opens session search panel (left of transcript, or modal?)
- Full-text search across ALL session titles + transcripts
- Results list: session title + match snippet (highlighted term)
- Click result → load session + scroll to match line
- Snippet shows 60 chars context around match

**UX flow:**
1. Cmd+Shift+F: search panel appears
2. Type query: "database optimization"
3. Results show:
   - "Thread #32 — about databases" — snippet: "…the **database optimization** process…"
   - "Onboarding" — snippet: "…see **database optimization** best practices…"
4. Click result → open session, scroll to first match
5. Escape closes panel

**Hard part:** Full-text index across 2000+ sessions + their transcripts (cost = load all paged history, or build disk index)

**Files:**
- `src/ecs/main_pane_system.h` — Cmd+Shift+F handler, panel UI (~80 lines)
- New: `src/search/session_index.h` — build + query full-text index (~120 lines)
- `src/ecs/loader_system.h` — trigger index build when session list loads (~15 lines)

**Dependencies:**
- Needs paged history loader to have populated disk cache (existing via api::disk_cache)
- Index built on-demand first load, cached in memory

**Testing:**
- E2E script can't press Cmd+Shift+F (command chord blocked by harness; see constraint note below)
- Unit test: `tests/unit/search_index_test.cc` — index building, query matching, snippet extraction
- Manual: screenshot Cmd+Shift+F panel with known query

**Constraint note:** Cmd+Shift+F cannot be tested via e2e scripts (harness maps Cmd→Ctrl, never honors Shift modifier on Super). Can be tested by `# env:` forcing `sessionSearchOpen = true` in AppComponent.

---

### Gap #2c: Command palette (Cmd+K) — Size: Medium
**What ships:**
- Cmd+K opens palette (modal, center of screen)
- Fuzzy search over: sessions (title), menu commands (New, Settings, etc.), smart views (Blocked, Review, etc.)
- Results ranked by fuzzy score + recency
- Return = select (open session / run command)
- Up/Down navigate results (or type to filter)
- Esc/click-outside closes

**UX flow:**
1. Cmd+K: palette appears, focus in search field
2. Type "block" → results: [Session "Blocked items", Command "Show Blocked view", etc.]
3. Cmd+K again → cycles through top 3 recent sessions (like Cmd+Tab)
4. Return on session → opens in active tab
5. Return on command → runs it

**Hard part:** Fuzzy ranking, result icons (session icon, command icon, view icon)

**Files:**
- New: `src/ecs/command_palette_system.h` — modal UI, input, results rendering (~180 lines)
- New: `src/search/fuzzy.h` — simple fuzzy-match scoring (~40 lines, copy from standard lib if available)
- `src/ecs/main_pane_system.h` — Cmd+K handler (~10 lines)

**Dependencies:**
- None

**Testing:**
- E2E: `tests/ui/command_palette.e2e`
  - `# env:` set palette open, search results
  - Assert palette renders, nav with Up/Down, Return selects, Esc closes
  - Cannot test Cmd+K press itself (Super+K unbound in harness)

---

### Gap #2d: Snippet highlighting in sidebar search (Polish) — Size: Small
**What ships:**
- Sidebar search (existing) now shows a snippet under the title
- Match term in snippet is highlighted (bold/accent color)

**UX flow:**
1. Type in sidebar search: "memory"
2. Row shows: "Session title" + "…we discussed **memory** management…"
3. Click row → opens session

**Hard part:** None; snippet extraction is straightforward

**Files:**
- `src/ecs/sidebar_system.h` — snippet rendering in search row (~40 lines)

**Testing:**
- E2E: `tests/ui/sidebar_search_snippet.e2e`
  - Mock search results with known snippet, assert render

---

## Settings & Preferences (8 gaps → 6 real gaps)

### Gap #3a: Send key configuration (Return vs Cmd+Return) — Size: Small
**What ships:**
- Settings → General section (new) → radio: "Send with" — Return / Cmd+Return
- Default: Return (users expect)
- Choice persists in Settings JSON

**UX flow:**
1. Cmd+, opens Settings
2. "Send message with:" radio buttons (currently Return is hardcoded)
3. Select Cmd+Return
4. Close Settings (choice saved)
5. Composer now requires Cmd+Return to send; plain Return = newline (not implemented yet; would need single-line mode)

**Hard part:** Composer single-line vs multi-line toggle (afterhours text_input is multi-line; restricting to single-line requires a custom wrapper or a feature in text_input)

**Files:**
- `src/settings.h` + `src/settings.cpp` — add `get/set_send_key()` getter/setter (~10 lines)
- `src/ecs/settings_system.h` — radio control in General section (~25 lines)
- `src/ecs/main_pane_system.h` — wire send_key setting to Enter/Cmd+Return handler (~15 lines)

**Dependencies:**
- Current composer is multi-line; making Shift+Return = newline (if Return sends) requires mode awareness

**Testing:**
- E2E: `tests/ui/send_key_config.e2e`
  - `# env:` set send key to Cmd+Return
  - Type in composer, press Return → assert no send
  - Cmd+Return → assert send
  - Toggle setting, assert behavior swaps

---

### Gap #3b: Show/hide timestamps setting (Polish) — Size: Small
**What ships:**
- Settings → Chat Behavior → toggle "Show timestamps"
- When off: transcript rows show no time
- When on: time shown (current behavior)

**UX flow:**
1. Settings → Chat Behavior
2. Toggle "Show timestamps" off
3. Transcript hides all times (rows are smaller)
4. Toggle back on → times return
5. Persists across relaunch

**Hard part:** None; straightforward conditional render

**Files:**
- `src/settings.h` + `src/settings.cpp` — add getter/setter (~10 lines)
- `src/ecs/settings_system.h` — toggle in Chat Behavior section (~15 lines)
- `src/ecs/main_pane_system.h` — conditional time render (~5 lines)

**Testing:**
- E2E: `tests/ui/timestamps_toggle.e2e`
  - `# env:` set timestamps enabled/disabled
  - Assert time text visible/hidden in rows

---

### Gap #3c: Typeface picker (system/serif/rounded/mono) — Size: Small
**What ships:**
- Settings → Appearance → dropdown "Typeface"
- Options: System (San Francisco), Serif (Georgia), Rounded (Avenir), Mono (Menlo)
- All transcript text reflows with new font
- Choice persists in Settings

**UX flow:**
1. Settings → Appearance
2. Dropdown "Typeface: [System ▼]"
3. Select "Serif" → all text reflows to Georgia
4. Readable, no layout breaks
5. Close Settings → choice saved

**Hard part:** Fonts must be available on macOS (all are system defaults)

**Files:**
- `src/settings.h` + `src/settings.cpp` — add `get/set_typeface()` (~10 lines)
- `src/ecs/settings_system.h` — dropdown in Appearance section (~20 lines)
- `src/ui/theme.h` — font selection logic (~30 lines)

**Dependencies:**
- FontManager must support loading multiple typeface families (check if it does)

**Testing:**
- E2E: `tests/ui/typeface_picker.e2e`
  - `# env:` set typeface to Serif
  - Assert transcript text renders in Georgia (sample phrase visible)

---

### Gap #3d: Text weight picker (user vs assistant) — Size: Small
**What ships:**
- Settings → Appearance → two dropdowns: "User message weight" / "Assistant message weight"
- Options: Light / Regular / Bold
- Choice persists

**UX flow:**
1. Settings → Appearance
2. "User message weight: [Regular ▼]" → select Bold
3. "Assistant message weight: [Regular ▼]" → leave Regular
4. Transcript: user messages now bold, assistant text normal weight
5. Close → saved

**Hard part:** None; straightforward font-weight application

**Files:**
- `src/settings.h` + `src/settings.cpp` — add getters/setters (~15 lines)
- `src/ecs/settings_system.h` — two dropdowns (~30 lines)
- `src/ecs/main_pane_system.h` — apply weights to user/assistant text (~20 lines)

**Testing:**
- E2E: `tests/ui/text_weights.e2e`
  - `# env:` set user message weight to Bold
  - Assert user row text is visibly bolder than assistant row

---

### Gap #3e: Theme picker with rotation — Size: Medium
**What ships:**
- Settings → Appearance → "Theme:" dropdown + button "Edit Theme…"
- Preset options: Dark, Light, System, (plus 0–2 user-created custom themes, stored in Settings)
- Bonus: "Rotate themes" mode — radio button "Static / Rotate"
  - When Rotate: choose interval (5 min / 15 min / 30 min / 1 hour)
  - App auto-cycles through all themes at interval
  - Current theme badge shows in dropdown

**UX flow:**
1. Settings → Appearance
2. Dropdown shows: "Dark ✓", "Light", "System", [custom themes if any]
3. Select "Light" → theme changes immediately
4. Radio "Rotate themes" → enable + pick "Every 15 min"
5. Close → theme rotates every 15 min through [Light, Dark, System, …]
6. On relaunch: Rotate setting persists, theme rotates as configured

**Hard part:** Timer loop for rotation (afterhours likely has update hook; check)

**Files:**
- `src/settings.h` + `src/settings.cpp` — add `get/set_theme_rotate_enabled()`, `get/set_theme_rotate_interval_secs()` (~20 lines)
- `src/ecs/settings_system.h` — dropdown + radio in Appearance (~50 lines)
- New: `src/ecs/theme_rotation_system.h` — timer-based theme cycling (~60 lines)

**Dependencies:**
- Needs render-loop integration (per-frame time delta)

**Testing:**
- E2E: `tests/ui/theme_rotation.e2e`
  - `# env:` enable rotation, set interval to 5 sec (fast for test)
  - Wait, assert theme switches
  - Toggle rotation off, assert theme stops switching
  - Disable rotation in Settings, restart app, assert Rotate is off

---

### Gap #3f: Custom theme editor — Size: Medium
**What ships:**
- Settings → Appearance → "Edit Theme…" button → modal editor
- Panel shows 11 color swatches (primary, secondary, accent, bg_0, bg_1, panel_bg_2, text_primary, text_faint, text_selected, success, error)
- Each swatch has a color picker (click → native macOS color picker)
- Syntax palette: 8 sub-palette rows (comments, keywords, strings, numbers, symbols, markup, diff added, diff removed)
- Right side: live preview card showing theme colors in action
- Button: "Save as new theme" → name input → store in Settings

**UX flow:**
1. Settings → Appearance → "Edit Theme…"
2. Editor modal appears with swatches + preview
3. Click primary swatch → macOS color picker
4. Choose new color → swatch updates + preview reflows
5. Repeat for 3–4 colors
6. "Save as new theme" → input "Ocean Blue"
7. Theme saved; dropdown now shows "Ocean Blue" as an option
8. Switch back to Dark, then to Ocean Blue → works
9. Close modal (changes persist if saved; discard if dismissed without saving)

**Hard part:** Live preview (must show theme in use); native color picker integration; theme persistence

**Files:**
- New: `src/ecs/theme_editor_system.h` — modal UI, color picker integration (~200 lines)
- `src/settings.h` + `src/settings.cpp` — add `get/set_custom_themes()` (list of theme dicts) (~30 lines)
- `src/ui/theme.h` — custom theme loading / merging with base theme (~40 lines)

**Dependencies:**
- Must store custom themes as JSON in Settings (dict of color name → RGB hex)

**Testing:**
- E2E: `tests/ui/custom_theme_editor.e2e`
  - Open editor, change primary color, save as "Test Theme"
  - Assert "Test Theme" appears in dropdown
  - Select it, assert colors applied
  - Restart app, assert custom theme persists
  - Delete theme from dropdown, assert it's gone

---

## Keyboard & Shortcuts (5 gaps → 5 real gaps)

### Gap #4a: Global hotkey (Cmd+Shift+Space) — Size: SMALL, not Medium

> **The global-hotkey machinery already exists.** `src/native_extras.mm`
> registers a Carbon hotkey and — importantly — gates it on app focus, so it
> does not steal the chord from other apps while hanabi is in the background.
> That focus-gating was hard-won and is the expensive part. This gap is
> "register a SECOND chord against the existing mechanism and point it at the
> palette", not "add global hotkey support". See
> `native-notifications-attachments.md`, which verified the existing one.

**What ships:**
- From anywhere (e.g., browser, Slack), press Cmd+Shift+Space
- Hanabi comes to foreground (or launches if closed)
- Command palette (Cmd+K feature, #2c) opens immediately
- Type to search sessions / commands
- Return opens selected item
- Esc dismisses palette (but hanabi stays foreground)

**UX flow:**
1. Browser in foreground
2. Press Cmd+Shift+Space → hanabi window appears, palette open
3. Type "memory" → sessions matching "memory" appear
4. Press Return → that session opens, palette closes
5. Hanabi stays foreground (another Cmd+Space closes it)

**Hard part:** Carbon hotkey registration + window bring-to-front logic; macOS application focus

**Files:**
- New: `src/native_extras_hotkey.mm` — Carbon RegisterEventHotKey() (~80 lines)
- `src/ecs/main_pane_system.h` — hotkey handler → palette open + palette selection (~15 lines)
- `src/main.cpp` or app delegate — register hotkey on startup (~10 lines)

**Dependencies:**
- Requires #2c (command palette) to be built first

**Testing:**
- Manual only (cannot script global hotkey in e2e). Screenshot:
  - Cmd+Shift+Space from desktop → hanabi appears + palette open
  - Type + Return → opens session

**Constraint note:** Global hotkey cannot be tested in e2e (no script access to OS-level keys). Verification is manual testing only.

---

### Gap #4b: Keyboard shortcut recorder (in Settings) — Size: Medium
**What ships:**
- Settings → Shortcuts tab (new section)
- List of bindable commands: New Session, Settings, Find, etc.
- Each row shows current binding (e.g., "Cmd N")
- "Record" button → recorder mode: press desired key combo, app captures + detects conflicts
- Conflict alert: "Cmd+N is already bound to [other command]"
- Save new binding → applies immediately + persists

**UX flow:**
1. Settings → Shortcuts
2. Scroll to "New Session", currently "Cmd N"
3. Click "Record" → key listener active, instruction shows
4. Press Cmd+Shift+N → detected as conflict (already bound to "Bring forward + start task")
5. Alert: "Already in use by Bring forward + start task. Choose another."
6. Try Cmd+Option+N → accepted
7. Binding saved, row now shows "Cmd+Option+N"
8. Close Settings, verify Cmd+Option+N now starts a task

**Hard part:** Capturing arbitrary key chords; conflict detection; persistence; rebinding the handler

**Files:**
- `src/ecs/settings_system.h` — Shortcuts tab + recorder UI (~100 lines)
- `src/keys.h` — enhance key capture to return parsed chord name (~40 lines)
- `src/ecs/input_bindings.h` (new or enhanced) — registry of command → key binding (~80 lines)

**Dependencies:**
- Key capture logic must be wired throughout (every command handler must check `input_bindings` before hardcoded key)

**Testing:**
- E2E: `tests/ui/shortcut_recorder.e2e`
  - Open Settings → Shortcuts
  - `# env:` simulate key press (e.g., set pending key capture to Cmd+Option+N)
  - Assert binding updated + persists

**Constraint note:** Cannot directly test key-press capture (harness limitation). Can test settings persistence + binding lookup.

---

### Gap #4c: Composer keyboard shortcuts — Size: Small
**What ships:**
- Enter: send (or Cmd+Return if configured, via #3a)
- Shift+Enter: newline (if Return sends)
- Option+Return: always newline (regardless of send-key setting)
- Up/Down: history walk (previous/next sent message)
- Cmd+A: select all
- Cmd+C: copy (existing)

**UX flow:**
1. Focus composer
2. Type "hello"
3. Shift+Enter → newline (caret moves down)
4. Type "world"
5. Enter → message sent
6. Composer clears
7. Press Up → message history recalled ("hello\nworld")
8. Press Up again → previous message (if any)
9. Press Down → step forward in history
10. At newest, Down does nothing

**Hard part:** History storage per-session; caret position preservation during history walk

**Files:**
- `src/ecs/main_pane_system.h` — Up/Down handler in composer + history iteration (~50 lines)
- New: `src/composer/history.h` — per-session message history store (~40 lines)

**Dependencies:**
- History stored in memory (cleared on app quit; no persistence)

**Testing:**
- E2E: `tests/ui/composer_history.e2e`
  - `# env:` inject messages into history
  - Simulate Up key → assert first message recalled + rendered
  - Simulate Down → assert history steps
  - At end, Up wraps or does nothing (verify behavior)

---

### Gap #4d: Navigation shortcuts (arrow keys in lists/menus) — Size: Small
**What ships:**
- Command palette (when open): Up/Down navigate, Return selects, Esc closes
- Smart-view list (if collapsible): Up/Down move selection (future; not all views need this yet)
- General pattern: any list / dropdown can use Up/Down

**UX flow:**
1. Cmd+K opens palette
2. Results: ["Session A", "Session B", "New Session" command]
3. Up/Down moves highlight bar over results
4. Return selects highlighted item
5. Esc closes palette (no selection)

**Hard part:** None; straightforward input handling

**Files:**
- Palette system (from #2c) includes Up/Down nav

**Testing:**
- E2E: `tests/ui/palette_navigation.e2e`
  - Palette open via `# env:` (since Cmd+K can't be pressed)
  - Simulate Up/Down keys
  - Assert highlight moves, Return selects

---

### Gap #4e: Find shortcuts (Cmd+G, Cmd+Shift+G) — Size: Small
**What ships:**
- Cmd+F: opens find bar (existing)
- Cmd+G: next match (scroll into view, advance counter)
- Cmd+Shift+G: previous match
- Escape: close find bar

**UX flow:**
1. Cmd+F → find bar appears
2. Type "error" → 5 matches, "1 of 5" shown, first match highlighted
3. Cmd+G → advance to match 2, "2 of 5"
4. Cmd+G → match 3, "3 of 5"
5. Cmd+Shift+G → step back to match 2, "2 of 5"
6. Escape → find bar hides

**Hard part:** None; wiring into existing find bar

**Files:**
- `src/ui/find_highlight.h` — enhance to support next/prev movement (~30 lines)
- `src/ecs/main_pane_system.h` — Cmd+G / Cmd+Shift+G handlers (~15 lines)

**Dependencies:**
- None; leverages existing find system

**Testing:**
- E2E: `tests/ui/find_navigation.e2e`
  - Find bar open via `# env:`
  - Cmd+G / Cmd+Shift+G simulate key press
  - Assert match counter increments/decrements + view scrolls
  - Esc closes

**Constraint note:** Cmd+G / Cmd+Shift+G cannot be pressed directly in e2e (Super+G unbound in harness), but the find bar handlers can be tested via `# env:` state injection.

---

## Commit Strategy: Ship in This Order

### Batch 1 (Day 1) — Core search foundation
1. **Find operators** (#2a, ~80 lines)
   - Delivers: Power-user find filtering (is:thinking, has:tool, etc.)
   - No other dependencies
   - Tight, reviewable diff

2. **Command palette** (#2c, ~250 lines)
   - Delivers: Cmd+K discoverability for sessions + commands + views
   - Foundation for global hotkey (batch 3)
   - Reviewable as pure UI (no data layer changes)

### Batch 2 (Day 2) — Settings consolidation
3. **Send key + timestamps + typeface** (#3a, #3b, #3c, ~80 lines total)
   - Three small settings all in Settings modal
   - Can land as one PR: "Settings: configurable send key, timestamps, typeface"
   - Low risk, high user satisfaction

4. **Text weights + theme rotation** (#3d, #3e, ~140 lines)
   - "Settings: appearance enhancements (text weights, theme rotation)"
   - Theme rotation needs render-loop timer but is otherwise isolated

### Batch 3 (Day 3) — Advanced keyboard
5. **Find next/prev** (#4e, ~50 lines)
   - Composer history was in this bundle; it is planned in `composer.md`
     instead, which owns the composer. Build it from there.
   - Tight scope, high daily-use value
   - No modal/UI churn

6. **Custom theme editor** (#3f, ~300 lines)
   - Largest single PR; bundle alone
   - "Settings: custom theme editor"
   - Modal UI + color picker + persistence

### Batch 4 (if time permits) — Advanced features
7. **Session search** (#2b, ~220 lines)
   - "Search: full-text session search (Cmd+Shift+F)"
   - Needs on-demand index build; can be async
   - Manual test only (hotkey blocked)

8. **Global hotkey + shortcut recorder** (#4a, #4b, ~280 lines)
   - "Keyboard: global hotkey (Cmd+Shift+Space) + shortcut recorder"
   - Spans native extras + settings; complex
   - Global hotkey needs manual testing (not automatable)

---

## Testing Constraints & Workarounds

### Cmd-chord limitation
Scripts cannot press Cmd (Super). The harness maps `Cmd+` → `Ctrl+` and never honors Super-Key. Affected shortcuts:
- Cmd+K (command palette)
- Cmd+Shift+F (session search)
- Cmd+Shift+Space (global hotkey)
- Cmd+G / Cmd+Shift+G (find next/prev)

**Workaround:** Use `# env:` directives in e2e to inject state (`showCommandPalette = true`, `sessionSearchOpen = true`, etc.) and test logic + rendering, not the keystroke itself.

### Single-line composer constraint
Current text_input is multi-line. Implementing "Shift+Enter for newline" (when Return sends) requires either:
1. Custom composer wrapper with single-line mode
2. Feature request to afterhours text_input
3. Parse-and-reject multi-line input on send

**Deferred:** Gap #3a ships with "Return sends (fixed), Cmd+Return planned" note in Settings; single-line mode is a follow-up.

---

## Total Effort Estimate

| Batch | Gaps | Est. Lines | Est. Hours | Risk |
|-------|------|-----------|-----------|------|
| 1 | Find ops, Palette | ~330 | 8–10 | Low (UI only) |
| 2 | Settings (3 + 2) | ~220 | 6–8 | Low (Config + render) |
| 3 | Composer + Theme | ~400 | 10–12 | Medium (History state, modals) |
| 4 | Session search, Hotkey | ~500 | 12–16 | High (Indexing, native code) |
| **Total** | **16** | **~1450** | **36–46** | **Medium** |

---

## Files Modified/Created

### Modified
- `src/settings.h` / `src/settings.cpp` — all preference getters/setters
- `src/ecs/main_pane_system.h` — find/palette/composer/hotkey handlers
- `src/ecs/settings_system.h` — all Settings tab UI
- `src/ecs/sidebar_system.h` — snippet highlighting (if doing)
- `src/ui/theme.h` — font loading, custom theme merging
- `src/keys.h` — enhanced key capture (if doing shortcut recorder)

### New
- `src/ui/find_operators.h` — find operator parsing + evaluation
- `src/ecs/command_palette_system.h` — Cmd+K modal
- `src/search/fuzzy.h` — fuzzy-match scoring
- `src/search/session_index.h` — full-text index building + query
- `src/ecs/theme_rotation_system.h` — timer-based theme cycling
- `src/ecs/theme_editor_system.h` — custom theme editor modal
- `src/ecs/theme_rotation_system.h` — auto-rotate logic
- `src/composer/history.h` — per-session message history
- `src/native_extras_hotkey.mm` — Carbon global hotkey
- `src/ecs/input_bindings.h` — command → key binding registry

### Tests
- `tests/ui/find_operators.e2e`
- `tests/ui/command_palette.e2e`
- `tests/ui/palette_navigation.e2e`
- `tests/ui/send_key_config.e2e`
- `tests/ui/timestamps_toggle.e2e`
- `tests/ui/typeface_picker.e2e`
- `tests/ui/text_weights.e2e`
- `tests/ui/theme_rotation.e2e`
- `tests/ui/custom_theme_editor.e2e`
- `tests/ui/composer_history.e2e`
- `tests/ui/find_navigation.e2e`
- `tests/ui/shortcut_recorder.e2e`
- `tests/unit/search_index_test.cc`

---

## Recommended Ship Order (First Three)

**If picking three to ship first for maximum user value per effort:**

1. **Command Palette (Cmd+K)** — ~250 LOC, 8–10 hrs
   - Solves discoverability for every feature
   - Foundation for global hotkey
   - No other dependencies

2. **Composer History (Up/Down)** — ~90 LOC, 3–4 hrs
   - Every user expects this (Discord, Slack, iMessage have it)
   - Self-contained, low risk

3. **Settings: Send Key + Timestamps + Typeface** — ~110 LOC, 4–5 hrs
   - Addresses three user preference types (behavior, UX, appearance)
   - All bundled in Settings, one coherent PR
   - Zero risk (config-only, no logic)

**Total for first three: ~450 LOC, 15–19 hrs.** Ready to ship by end of Day 1.
