# Hanabi — Ponytail Refactor Review

Read-only code-quality review of `hanabi` (C++23 + afterhours ECS/UI + Sokol/Metal).
Scope: `src/` app code (ECS systems, API layer, UI theme/icons, top-level). Vendored
afterhours and tests excluded. Methodology: ponytail (lazy-senior-dev) — does this code
need to exist? is there a stdlib/platform way? one line before fifty? duplicate logic that
should be shared? dead code / unused params? over-abstraction?

**No source was edited.** This doc is the only file written.

---

## TL;DR — Top 10 findings, ranked (quick wins first)

| # | Kind | Where | One-liner | Effort | Risk |
|---|------|-------|-----------|--------|------|
| 1 | Dead code | `sidebar_system.h` | `render_time_groups` + whole `TimeBucket` enum/label/key/`time_bucket` machinery is uncalled (~150 lines) | trivial delete | very low |
| 2 | Dead code | `main_pane_system.h:2132` `estimate_height`, `:2881` `tool_count` | both defined, never called | trivial delete | very low |
| 3 | Redundant work | `main_pane_system.h:79` `app_singleton()` | re-queries the ECS 6×/frame when `for_each_with` already holds `app` | small | low |
| 4 | Dup helper | `main_pane_system.h` vs `format.h` | third relative-time impl (`tool_duration` is a 4th time-format); `relative_time` already exists | small | low |
| 5 | Dup helper | `upper`/`lower`/title-case scattered | 4 hand-rolled ASCII case flippers across 3 files | small | low |
| 6 | Copy-paste constants | `main_pane_system.h` / `sidebar_system.h` | glyph-slot width, inset, row-height magic numbers duplicated per-file | small | low |
| 7 | Dup builder | button configs (`icon_btn`, composer, send) | same ~12-line "chrome button" ComponentConfig repeated ~8× | medium | low |
| 8 | RISKY mirror | `bubble_height` ↔ `render_bubble`; `rich_body_h` ↔ `render_rich_body`; `tool_*_height` ↔ tool render | measure/render height math duplicated by hand, MUST stay in sync | large | HIGH |
| 9 | Dup line-scanner | `main_pane_system.h` (9 copies) | the `while(start<=size){find('\n')...}` segment loop is copy-pasted ~9× | medium | medium |
| 10 | Over-abstraction / size | `main_pane_system.h` (3349 lines) | one System owns transcript + 5 digest views + composer + tool rows + markdown + redaction | large | medium |

---

## 1. DUPLICATE CODE (with file:line)

### 1a. Height measure ↔ render mirror — the classic risky pair *(finding #8, HIGH risk)*
The transcript virtualizes, so every renderable computes its height in a **measure**
function and again in a **render** function, and the two must produce identical pixels or
spacers drift and the scrollbar lies. These pairs are hand-maintained twins:

- `bubble_height()` (`main_pane_system.h:2359`) ↔ `render_bubble()` (`:3120`). The author-row
  term (`showAuthor ? (kAuthorH + kAuthorGap) : 0`), the `kTurnGapTop + 12.0f`, the user
  bubble cap `520.0f`, the fold-button `kFoldBtnH` add — all appear **twice**, computed
  independently. A comment at `:3117` even says "Must mirror bubble_height's author-row term
  exactly" — an explicit admission the invariant is manual.
- `rich_body_h()` (`:2400`) ↔ `render_rich_body()` (`:2960`). Both re-scan the body,
  detect code fences, count inner lines, and sum `code_block_h`/`kLinePitch*0.5`/`segLines*pitch`.
  Two full copies of the same fence-scanning loop.
- `tool_out_height()` (`:3040`) ↔ `render_tool_block()` expanded panel (`:3252`) — both
  cap at `kToolOutLines` and count `\n`; `tool_pile_height` (`:3065`) ↔ `tool_pile` render.

**Concrete fix:** invert the dependency — make the render pass RETURN the height it
actually laid out (accumulate `y` as entities are emitted), and have the measure pass call
a single shared `layout_*` routine that both consume. Minimum viable version: extract the
per-segment "advance y by this segment's height" into ONE `segment_advance(line, perLine)`
helper used by both `rich_body_h` and `render_rich_body`, so the fence/blank/wrapped-line
arithmetic lives in exactly one place. Highest-value structural refactor in the file.
**Risk: high** (it's the core of virtualization) — do it behind the existing headless tests.

### 1b. Relative-time formatting — 3–4 implementations *(finding #4)*
- `fmtutil::relative_time()` (`format.h:10`) — canonical: now/m/h/d/w/mo/y.
- `sidebar_system.h:334 row_time_label()` — reimplements now/m/h/d then switches to an
  absolute `strftime` date. Genuinely different output (absolute date tail), but the
  now/m/h/d ladder is copied verbatim including the `24*60*60` constant (also at `:288`).
- `main_pane_system.h:2843 tool_duration()` — ms/s/m formatting; different domain
  (durations not ages) so arguably justified, but it's a 4th bespoke time formatter.

**Fix:** have `row_time_label` call `fmtutil::relative_time` for the < 7d branch and only
hand-roll the absolute-date tail. Pull the `kDay = 24*60*60` constant into `format.h`.

### 1c. ASCII case conversion — 4 hand-rolled flippers *(finding #5)*
- `main_pane_system.h:1150 upper()` (`c - 32`)
- `main_pane_system.h:2242 fence_lang()` (`c - 'a' + 'A'` inline)
- `sidebar_system.h:406 lower()` (`std::tolower`)
- `sidebar_system.h:1300 display_folder_name()` (two more `c - 32` title-case loops)

**Fix:** one `fmtutil::ascii_upper/ascii_lower/ascii_title` in `format.h`. Stdlib note:
`std::toupper`/`tolower` (cctype) already exist — the bespoke `c-32` is exactly the
"custom code that duplicates stdlib" ponytail flags. `disk_cache.cpp:118` has yet another
char-class check that could share an `is_token_char`.

### 1d. Secret redaction / markdown helpers referenced across files *(low, mostly OK)*
`redact_secrets`, `strip_inline_md`, `normalize_md_lines`, `char_budget`, `estimate_height`
live only in `main_pane_system.h` and are referenced by `transcript_render_cache.h`. Not
truly duplicated, but they are general string utilities marooned inside a giant UI System
class as `static` members — they belong in `util/format.h` (or a `util/mdtext.h`) where
they're testable and reusable. See finding #10.

### 1e. Line-splitting scan loop — ~9 copies *(finding #9)*
The idiom
```
size_t start = 0;
while (start <= s.size()) { size_t nl = s.find('\n', start); size_t end = nl==npos?s.size():nl; ... if (nl==npos) break; start = nl+1; }
```
appears ~9× in `main_pane_system.h` (`count_lines`, `first_n_lines`, `rich_body_h`,
`render_rich_body`, `normalize_md_lines`, the code-fence inner scans, `render_tool_block`
output split). **Fix:** one `for_each_line(sv, fn)` helper (a `std::string_view` splitter,
~8 lines) collapses all of them and removes a class of off-by-one bugs. C++23 has
`std::views::split` but the manual splitter is fine and clearer here.

### 1f. Section-header / row builders — divergent copies
- Two `section_label` helpers: `main_pane_system.h:1157` (Home sections, uppercase +
  letter-spaced) and `sidebar_system.h:1092` (sidebar VIEWS/FOLDERS). Similar intent,
  different enough to justify separate — but note they've **drifted** (different padding,
  casing, font token) with no shared base.
- Three "count column" alignment schemes: sidebar has a carefully-shared
  `label_col_w` + `kCountColW`/`kCountRightPad` (good, DRY within the file), but the digest
  cards in `main_pane_system.h` re-derive title/column widths with their own `char_budget`
  math. Cross-file, the "reserve trailing fixed column, size label in px because no
  flex-grow" pattern is reinvented in `smart_item`, `render_group_header`, `render_chat_row`,
  `tool_row`, and the digest card. That's the afterhours gap #18 workaround copy-pasted 5×.
  **Fix:** a small `fit_columns(rowW, leadW, {optional trailing widths...}, titleMin)`
  helper returning the label width + which optional columns survive. High leverage: every
  one of those blocks has its own "no-overflow / drop column at narrow width" comment.

### 1g. Chrome-button ComponentConfig — repeated ~8× *(finding #7)*
`sidebar_system.h` has `icon_btn` (`:? `) and `icon_btn_sprite`; `composer_system.h`
close/cancel/start; `main_pane_system.h` composer Send / fold button / code Copy button —
all repeat the same `.with_custom_background + hover + text_color + font + center align +
click_activation(Press) + roundness` block. `presets.h::Button` exists but isn't used by
these. **Fix:** extend `presets.h` with `preset::IconButton(sprite, fallback)` and
`preset::PrimaryButton(label, enabled)` / `preset::SecondaryButton` and route these
call sites through them. The enabled/disabled color ternary (`sendEnabled ? primary :
disabled_bg`, repeated for bg/hover/text) is itself copy-pasted 3×.

---

## 2. PONYTAIL FINDINGS (over-build / dead / needless)

### 2a. Dead code — delete outright *(findings #1, #2)*
- **`render_time_groups`** (`sidebar_system.h:1600`, ~40 lines) + its entire support cast:
  `enum TimeBucket`, `time_bucket()` (`:283`, ~25 lines of `mktime`/`localtime_r`),
  `time_bucket_label()`, `time_bucket_key()`. A comment at `:? ` says the time-grouping was
  removed "per Gabe: do NOT day-bucket" and the function is "retained but unused so it can be
  re-enabled behind a toggle." That's ~150 lines of dead, tested-nowhere code kept on spec.
  YAGNI: delete it; git history is the toggle.
- **`estimate_height`** (`main_pane_system.h:2132`) — never called; `count_lines` +
  `rich_body_h`/`flat_body_h` superseded it. Delete.
- **`tool_count`** (`main_pane_system.h:2881`) — returns constant `1`; its own comment says
  "Piles sum by message count, so this is always 1; kept only for callers that ask
  per-message." No such caller exists. Delete.

### 2b. Unused params / no-op args
- `sub_agent_panel(... float colW)` — body ends `(void)colW;` (`:1358`). Drop the param.
- `render_tool_block` computes `truncated` then `(void)truncated;` (`:3343`) — dead local.
- `tool_meta_cluster`'s `count` is only meaningful when `showCount`; fine, but the badge
  path builds a `layers` icon + count even for piles where count==member-count is already
  in the header text (minor).

### 2c. `app_singleton()` re-query *(finding #3)*
`main_pane_system.h:79` runs a fresh `EntityQuery{force_merge}.whereHasComponent<AppComponent>()`
and is called 6× per transcript frame (`:2359,:2390,:2717,:3129,:3252`). But the System's
`for_each_with` already receives `app` and passes `AppComponent& app` down most call chains.
Thread the existing reference through (or cache one pointer at the top of
`render_transcript`) instead of re-scanning the entity table 6× every frame. Pure win.

### 2d. Copy-pasted layout magic numbers *(finding #6)*
`kGlyphW = 12.0f` (leading status-glyph slot) is defined in `sidebar_system.h`; the same
12px glyph slot is hardcoded inline in `main_pane_system.h` tool rows (`pixels(12)` for
chevron slot, `pixels(16)` icon). `kContentInset = 24.0f` lives in `main_pane_system.h`;
the sidebar uses `kRowLeftInset = 16` and a separate `14` search inset. Row heights: sidebar
`kRowHeight = 24`, digest cards `34/52`, tool rows `28`. These are per-file constants that
represent a shared design rhythm — several already have "must match the mock's 16px 24px"
comments. **Fix:** a `theme::layout` (or `ui/metrics.h`) home for `kContentInset`,
`kGlyphSlot`, `kRowHeight`, `kLinePitch` so the "single consistent margin" the comments keep
asserting is enforced by one symbol, not prose.

### 2e. Over-abstraction (minor)
- `SubGlyph`/`sub_glyph_for`/`sub_glyph_color`/`draw_sub_glyph` in `main_pane_system.h`
  duplicate the sidebar's `Glyph`/`glyph_color`/`draw_glyph` shape vocabulary (triangle/dot/
  ring). A comment says it "replicates the sidebar's shape vocabulary locally (no dependency
  on sidebar_system.h)." Two shape-drawing libraries for the same 4 shapes. Could share a
  `ui/status_glyph.h`. Low priority (the shapes are small) but it IS duplicated intent.
- `strip_parked_marker`/`strip_parked_prefix` in both big systems are already thin wrappers
  over `fmtutil::display_title` (good — someone did the right thing). The wrappers add nothing
  now; call `fmtutil::display_title` directly and delete the two forwarders.

---

## 3. RISKY DUPLICATION (must-stay-in-sync pairs) — flag every one

1. **`bubble_height` ↔ `render_bubble`** (`:2359`/`:3120`) — height vs layout, hand-mirrored.
   Comment at `:3117` confirms the invariant is manual. *Primary risk.*
2. **`rich_body_h` ↔ `render_rich_body`** (`:2400`/`:2960`) — duplicate fence-scan + line-sum.
3. **`flat_body_h` ↔ user-bubble render** (`:2440`/`:3145`) — both use `wrap_perline` + pitch.
4. **`tool_out_height` ↔ render_tool_block expanded panel** (`:3040`/`:3252`) — `kToolOutLines`
   cap + `\n` count replicated.
5. **`tool_pile_height` ↔ `tool_pile`** (`:3065`/render) — open-state sub-row height sum.
6. **`tool_block_height` ↔ `render_tool_block`** — `kToolRowGap + kToolRowH + kToolRowGap`.
7. **`tool_row` meta-cluster width math** (`:? ` "metaW += 44/42/...") ↔ `tool_meta_cluster`
   actual widths — the reserve math must equal the emitted box widths (44px dur, 42px badge,
   14px check) or the command column overflows. Two places encoding the same pixel widths.
8. **sidebar `row_time_label` now/m/h/d ladder ↔ `fmtutil::relative_time`** — parallel ladders.
9. **`tool_command`/`tool_node` fallback parsing** appears both in the renderer and mirrors
   `http_client.cpp`'s `[node] cmd` prefix convention — a display-time re-parse of a
   backend-time format. If the backend format changes, two parsers must change.

Each pair should ideally collapse to a **render-returns-height** design (measure = a dry run
of render) so there is one source of truth.

---

## Architecture-smell summary

**Is `main_pane_system.h` too big? Yes — 3349 lines, ~9× the next non-generated ECS file.**
It is not a cohesive "system"; it's ~6 features fused into one class:
1. main-pane dispatch + smart-view digests (Home/Blocked/Review/Starred/Archived),
2. the chat transcript (virtualization, scroll-anchor, load-older, pin-to-bottom),
3. the composer (send/stream/kickoff + 3 screenshot-demo env hooks),
4. tool-row / tool-pile rendering,
5. a mini markdown engine (fences, bold/code strip, bullets/rules, redaction),
6. text metrics (wrap/height estimation).

Cohesion is low: (5) and (6) are pure string/text utilities with no ECS or UI dependency and
should live in `util/` (testable headless — the repo already values headless-tested pure
logic, cf. `ecs::model`). (1) digests and (2) transcript share almost nothing but the header
helper. A clean split:
- `util/mdtext.h` — redact_secrets, strip_inline_md, normalize_md_lines, fence helpers,
  wrap/line-count/height metrics (all the pure functions). **Biggest single win** — pulls the
  risky measure/render math into a place it can be unit-tested against the renderer.
- `ecs/digest_view_system.h` — Home + the 4 digest views + `digest_card`/`skeleton_card`.
- `ecs/transcript_system.h` — the transcript, tool rows, composer.

The **other ECS systems are reasonably cohesive.** `tab_bar_system.h` correctly delegates all
pure logic to a headless-tested `model::` layer (open/close/reorder/scroll math) and keeps only
input+render — that is the pattern the whole codebase should follow, and it's the counter-example
that shows `main_pane_system.h` could do the same. `sidebar_system.h` (1936 lines) is large but
single-purpose (one sidebar); its main debt is the dead time-bucket machinery (#1) and the
column-fit workaround copied per row-type (#1f). The API layer (`http_client.cpp`) is dense but
appropriately modular (small `as_*` json helpers, one `derive_state`). The consistent theme:
**pure logic that belongs in a testable `util`/`model` layer is instead inlined as `static`
members of UI Systems, which is both what bloats the files and what makes the measure/render
mirrors risky.**

### Recommended order of attack
1. Delete dead code (#1, #2, 2b) — pure subtraction, ~200 lines gone, zero behavior change.
2. Thread `app` instead of `app_singleton()` (#3) — perf, trivial.
3. Consolidate string/time/case helpers into `util/format.h` (#1b, #1c, #1e, 2e forwarders).
4. Extract `util/mdtext.h` with the text-metrics + markdown functions (#1d, sets up #5).
5. Only then tackle the measure↔render collapse (#8/§3) behind the headless tests.
6. Column-fit helper (#1f) and button presets (#1g) as they're touched.

