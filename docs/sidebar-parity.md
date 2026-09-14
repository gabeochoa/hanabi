# The sidebar, against the reference

What differs between hanabi's sidebar and the reference's, where each
difference lives, and what the evidence for it is. Two sources, kept apart on
purpose:

- **source** — the reference's own code at the pinned revision
  `bffecaf665af75f878ac5c8f9f4468c8aa6d911c`
  (`Views/SessionRowView.swift`, `Views/SmartViewSidebar.swift`,
  `Views/SidebarColumn.swift`). Exact and quotable.
- **screenshot** — the user's own capture of 0.7.1 (private, never published,
  never in this repo). Good for *what is on screen*; it is one window at one
  size, so it is not a measurement.

Nothing here is a pixel claim. A pixel claim needs a capture of the pinned
build at a matched size, theme and fixture, which does not exist yet — the
build recipe is with the reference's tooling owner. Until then every row below
says which of the two sources it rests on, and the numbers from source are
points in the reference's coordinate space, not measured pixels in ours.

## Rows

| # | The reference | hanabi today | Evidence | Owner |
|---|---|---|---|---|
| 1 | Row is 34 pt tall, insets 6/8/6/4, one glyph slot 10 pt wide, 6 pt between glyph and text | **row height now 34** (was `theme::chrome::ROW` = 32); left inset still 9, glyph slot still 13 | source (`SessionRowView.measuredHeight`, `rowInsets`, `markWidth`, `glyphSpacing`) | `src/ecs/sidebar_system.h` |
| 2 | Leading glyph is the thread's STATE, in the state's colour: `!` for asking/blocked (orange), an arc for running (green), a chevron for a parent with children (blue), a dot for idle | one glyph, accent-coloured arc for activity; no per-state colour vocabulary, no parent chevron | source (`leadingGlyph`, exhaustive over `SessionState`) + screenshot | `src/ecs/sidebar_system.h`, `src/ecs/thread_model.h` |
| 3 | A parent row's chevron FOLDS its children in place | children are a separate sub-agent sidebar | source (`leadingGlyph` `.fold`, `foldButton`) | `src/ecs/sidebar_system.h` |
| 4 | Count column reads "N" or "running/total" ("1/46"), accent-coloured while any child runs, muted otherwise | **already matches** — `sub_agent_label` spells the same two forms and `sub_agents_live` drives the same two colours | source (`ChildActivity.label`) read against `src/ecs/thread_model.h` | — |
| 5 | Age is "now" / "5m" / "3h" / "Nd" — days for as long as the thread is old, never a calendar date | **now the same ladder** (`ecs::model::sidebar_age`); was the app-wide ladder that rolled into "2w", "3mo", "1y" and then "Jul 28" past a week | source (`displayAge`/`relativeAge`) | `src/ecs/thread_model.h` |
| 6 | Muted and archived marks ride beside the title (bell-slash, archivebox), theme dot before the count | muted mark yes; archived mark and theme dot no | source (`rowContent`) | `src/ecs/sidebar_system.h` |
| 7 | Second line: a label pill (micro, 1/4 padding) then the snippet, indented 17 pt | snippet line without the pill | source (`snippetLine`) | `src/ecs/sidebar_system.h` |

## Sections

| # | The reference | hanabi today | Evidence | Owner |
|---|---|---|---|---|
| 8 | The conversation list is in collapsible sections with a count in the header — PINNED (7), RECENTS (32); title uppercase bodyEmphasis, count as "(n)", chevron in a 10 pt slot 6 pt from the title | **now two sections**, PINNED and RECENTS, out of the same catch-all bucket (ordering, cap and search unchanged); every section header is uppercase and a plain count reads "(n)" | source (`SidebarSection.headerLabel`/`headerDetail`/`foldButton`) + screenshot | `src/ecs/sidebar_system.h` |
| 9 | The Views shelf is its own section titled "Views" with two header accessories, read at the pin rather than guessed from their icons: a **"+"** whose help reads "Save the current filter as a view" (`onSaveCurrent` → `SidebarColumn.saveCurrentAsView`: prompt for a name with a derived suggestion, build a saved filter from the current shelf + workspace + search text, add it to the store, then clear the search box), and a **sidebar.leading** toggle that collapses the column to the rail | a shelf with no header; hanabi HAS the collapse (`sb_collapse`) but in the panel's own header, and has **no saved-filter store at all** — its views are the five fixed ones, so "save the current filter as a view" has nothing to save into | source (`SmartViewSidebar.headerAccessories`, `saveCurrentButton`, `collapseToggle`, `RailNames.saveCurrent`, `SidebarColumn.saveCurrentAsView`) | `src/ecs/sidebar_system.h` — **OPEN**, see below |
| 10 | A Views row's attention count is a FILLED pill on ONE attention colour with white digits, bodyEmphasis, monospaced, 1/5 padding — Home, Blocked and Review badged alike | **now a filled pill on one colour** (`theme::attention_badge`, ink by `theme::on_fill`); the per-view colours are gone | source (`SmartViewSidebar.badgeView` → `filledPill`, no per-filter branch) + screenshot (three orange pills) | `src/ecs/sidebar_system.h`, `src/ui/theme.h` |
| 11 | Settings swaps the sidebar's LOWER half and leaves the shelf in place, because the shelf is how you get back | the settings pane list replaces the lower half when the sidebar is wide enough (`hosts_settings`) | source (`SidebarColumn`, the comment is explicit) | `src/ecs/sidebar_system.h`, `src/ecs/settings_system.h` |

## Changed in this candidate

Rows 1, 5, 8 and 10 above. Nothing is kept divergent on purpose. Two values in
row 10 are pixel questions a source reading cannot settle and a scaled
screenshot cannot either — the pill's exact fill value and its padding. The
RULE is matched now (one attention colour, white ink deepened for contrast);
the values come from the pinned capture, and until that exists this row is
open, not done.

Still open, in order of how much they show: the state-glyph vocabulary and
the parent chevron that folds children in place (2, 3), the Views header and
its accessories (9), the snippet's label pill (7), the archive mark and theme
dot (6). The header's own metrics — the 10 pt chevron slot, the 6 pt gap, the
4 pt vertical inset — are written down but not yet applied; they move pixels,
so they wait for the capture.

## The one capability hanabi does not have

Row 9's "+" is not a styling gap. It saves the CURRENT filter — shelf plus
workspace plus the words in the search box — as a new view the reader can
come back to, with a name they are prompted for. hanabi has five fixed smart
views and no store for user-defined ones: there is nothing behind that
button, and drawing it would be a control that does nothing, which this
codebase does not do (the same rule that keeps the microphone out of the
composer).

Classification: **Hanabi-only mismatch — a missing app capability**, not a
library defect and not an upstream gap. What it would take: a saved-filter
model (name, shelf, workspace, query), persistence beside the other sidebar
settings, a name prompt, and the store's add/rename/delete/reorder — the
reference's `SavedFilterStore` is the shape. Until that exists this row stays
OPEN in this table; it is not "done" and it is not "deliberately different".

The collapse toggle IS available — hanabi's own `sb_collapse` — so when the
Views header lands, that half of the accessory pair is real.

## Settings, measured against the pinned build (matched pair)

Both 1440x900 logical, dark, Settings open as a tab, synthetic fixtures:
reference `/tmp/hz-ref/ref_appearance_1x.png` (build bffecaf6 under a
private bundle id), candidate `/tmp/hz-shots/hz_appearance_1440.png`.
Neither is published. The reference's pane was scrolled by its own harness,
so these are structure and chrome differences, not a per-pixel diff of one
scroll position.

| # | The reference | hanabi today | Owner |
|---|---|---|---|
| S1 | The shelf stays: Home / Settings / Blocked / Review / Pinned / Archived above the settings pane list, Settings shown as the selected view | **matches now** (was: the whole column was replaced) | `src/ecs/sidebar_system.h` |
| S2 | The pane has no title, no subtitle and no close of its own | **matches now** (was: "Settings" + subtitle + x inside the pane) | `src/ecs/settings_system.h` |
| S3 | "Search settings" sits in the SIDEBAR, under Archived, with a magnifier | **now in the sidebar when hosted** (same field chrome as the conversation search; the sheet keeps its own row when it is a sheet) — unbuilt, uncaptured | `src/ecs/sidebar_system.h`, `src/ecs/settings_system.h` |
| S4 | Every settings-pane row carries a leading glyph (gear, paintbrush, speech bubble, bell, ...) | **now a glyph per row**, the Lucide drawing of each of the reference's SF symbols (SettingsView.icon → ten new atlas sprites) — unbuilt, uncaptured | `src/ui/settings_catalog.h`, `src/ecs/settings_system.h`, `scripts/gen_icons.py` |
| S5 | Content is a stack of CARDS: rounded, bordered, the section title outside and above, each row title + description inside | **now one card per pane** (rounded 10, hairline border, raised fill, title above); the reference's per-SECTION split within a pane needs a section field in hanabi's catalog and is its own change — unbuilt, uncaptured | `src/ecs/settings_system.h` |
| S6 | A boolean is a SWITCH at the right of its row | **now the library's toggle_switch** — one control, one checked state, click or keyboard toggles — on all seven on/off settings; the two-button pairs are gone (the "Off / Ping" desktop-alert control is a real three-way and stays segmented) — unbuilt, uncaptured | `src/ecs/settings_system.h` |
| S7 | The selected pane row is a restrained fill — accent at 25% over the surface, text lifted to full (HoverHighlight.selectedOpacity) | **now the same wash** (`theme::chrome::selected_on`, the rule the shelf already uses) with primary text; the filled accent button is gone — unbuilt, uncaptured | `src/ecs/settings_system.h` |
| S8 | Tab chips carry a leading glyph (a gear on Settings) | **now a gear in the status-mark slot** on the Settings chip (PuffinSurface.icon) — unbuilt, uncaptured | `src/ecs/tab_bar_system.h` |

S1 and S2 came out of these captures and are fixed. S3-S8 are open, each
with a visible difference and an owning file; none is guessed from the old
screenshot.

## What this list is not

It does not cover: the search field's own geometry, the drag-to-reorder
affordance, hover and selection fills, the footer, or the collapsed rail's
metrics. Those need the pinned capture to say anything honest about, because
the screenshot is one width and the source gives their rules but not their
rendered result.

## Appearance pane catalog — the reference's rows against hanabi's (from SettingsAppearanceTab.swift at fb84b1d4 and the first valid same-category capture, 2026-09-13)

The reference's Appearance pane, top to bottom, and what each row becomes here. "REAL" means the row drives an actual behaviour in hanabi; nothing here is a control that stores a value nobody reads.

| # | Reference row (section) | Reference storage / behaviour | hanabi today | Plan |
|---|---|---|---|---|
| A0 | origin legend capsule ("On this Mac") | `SettingsOriginLegend(pane:)` | pane title + subtitle + "This Mac: kept here only" line | drop title/subtitle/line; one capsule row like the reference's |
| A1 | **Icons** · Icon set: Normal / the reference's own | `iconTheme` — swaps the app's symbol set | one sprite atlas (Lucide) | OPEN until a second icon set exists in `resources/icons`; a segmented control with one real choice is a fake — not drawn |
| A2 | **Reading width** · Transcript width: Comfortable / Wide / Full width (+ help text) | `TranscriptWidth` 768 / 1024 / ∞ column, transcript AND composer follow | fixed 768 band (`kComposerReadCol`) and 736 (`kReadCol`) | REAL: new setting `transcript_width` (comfortable/wide/full); both bands read it |
| A3 | **Context** · Full context detail (toggle + help) | `contextDetail`: meter popover shows lifetime totals, cache split, last call, compaction, sub-agent rollup | hanabi's context meter popover exists (`contextMeter`) with a fixed level of detail | REAL: new setting `context_detail`; the popover shows the extra lines when on (whatever hanabi already has data for; lines without data are not invented) |
| A4 | **Composer** · Tool, thinking and delivery chips (toggle + help) | `showDisclosureChips`: chips open by default | hanabi has per-thread tool fold modes (`get_tool_fold`) and a global finished-subagents switch | REAL: new setting `disclosure_chips_open` = the default fold state for tool/thinking/delivery chips in a conversation with no per-thread choice |
| A5 | **Typeface** · Your messages / replies: design (System/Serif/Rounded/Monospaced) × weight (Light…Bold) + help | `MessageTypographySet` per side | per-side FONT (default/hyperlegible/mono via `user_font`/`assistant_font`) + ONE global weight | REAL: per-side weight (`user_weight`, `assistant_weight`, migrated from the global `font_weight`); design: hanabi ships Roboto, Atkinson Hyperlegible, JetBrains Mono — Serif and Rounded need a font file → OPEN for those two, the segmented control lists the designs hanabi has |
| A6 | **Theme** · One theme / Rotate themes (+ caption, per-thread hint, "Thread colours in the sidebar" toggle, theme list with Default/Use per theme) | `ThemeManager.rotateThemes`: each CONVERSATION gets its own theme; blocklist per theme; `sidebarThreadColour` tints rows | hanabi's `theme_rotate_secs` is a TIMER that flips light/dark — different semantics; no per-conversation theme; no row tint | per-conversation theme is its own feature (also the row menu's Theme ▸) → OPEN with a plan; the timer row is hanabi-only and moves off this pane; "Thread colours in the sidebar" lands with per-conversation theme |
| A7 | **Custom theme** · Create/Edit/Delete a palette of eleven colours | `ThemeManager.custom` | accent + find-highlight choices only | OPEN: the eleven-colour editor is a feature; hanabi's two swatch rows stay until it exists |

Layout facts to match (measured on the pair, 1280x720 @2x): content column x≈438..1130 (692 wide) vs hanabi 326..1242; section title outside and above each card; a card is one or more rows of glyph + title with the control right-aligned at the card's right edge; help text inside the card under its row in the secondary colour; toggles are the small system switch (≈38x22 at 1x), not 52x28; segmented controls are compact pill groups sized to their labels, right-aligned, only the selected segment filled.
