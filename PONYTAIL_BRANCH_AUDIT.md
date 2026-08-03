# Ponytail Branch Audit — hanabi

Lazy-senior-dev audit (ponytail, full intensity) of UNNECESSARY BRANCHING in `src/`.
Scope: if/else ladders, switch/case, ternaries, guard clauses, boolean gates.
Vendored `vendor/afterhours` excluded. This is a READ-ONLY audit — no source edited.

Legend: `delete:` dead branch · `yagni:` guards a mode nobody sets · `shrink:` same
logic fewer branches · `dup:` two branches share a body · `redundant-guard:` re-checks
something already guaranteed.

---

### 1. src/ecs/main_pane_system.h:2590 — redundant-guard `canSend &&` in the composer caption chain
The `else if (canSend && hasText)` is only reached after `if (!canSend)` (line 2580) took the
first branch on read-only backends. `canSend` is `const` (line 2307) and unmodified through the
chain, so once we're past `!canSend` it is ALWAYS true here.
```
if (!canSend) caption = "read-only …";
...
else if (canSend && hasText)   // canSend is provably true in this branch
```
Safe because: the earlier `!canSend` guard + `const` = the `else` arm implies `canSend`.
Replacement: `else if (hasText)`. **-1 term. Confidence: HIGH.**

### 2. src/main.cpp:540-571 — dup two branches with identical body in the blocked-count notifier
An if/else-if/else-if where the FIRST (`lastBlockedNotified < 0`, prime) and THIRD
(`blocked < lastBlockedNotified`, count dropped) branches have the SAME body:
`lastBlockedNotified = blocked;`. Only the middle branch (`blocked > last`) is special (it notifies).
The equal case correctly does nothing — but writing the same value is a no-op.
```
if (lastBlockedNotified < 0)            lastBlockedNotified = blocked;
else if (blocked > lastBlockedNotified) { …notify…; lastBlockedNotified = blocked; }
else if (blocked < lastBlockedNotified) lastBlockedNotified = blocked;
```
Safe because: writing `lastBlockedNotified = blocked` unconditionally at the end is
behavior-identical (prime/decrease already do it; equal writes the same value; increase writes it
after notifying). Replacement: `if (lastBlockedNotified >= 0 && blocked > lastBlockedNotified) { …notify… } lastBlockedNotified = blocked;`
**~-4 lines. Confidence: HIGH.**

### 3. src/ecs/main_pane_system.h:449-451 — dup `case EmptyGlyph::Inbox:` duplicates the `default:`
In the empty-state glyph draw switch, `case EmptyGlyph::Inbox:` falls straight through into
`default:` (shared body — draw the inbox rectangle). The label is redundant: `default` already
covers `Inbox` (and `None`).
```
case EmptyGlyph::Inbox:
default:
    afterhours::draw_rectangle_outline({cx-10,cy-8,20,16}, c);
    break;
```
Safe because: `default` catches every remaining enum value; deleting the `Inbox` label changes
nothing. Replacement: drop `case EmptyGlyph::Inbox:`. **-1 line. Confidence: HIGH.**

### 4. src/ecs/main_pane_system.h:395-396 — dup two switch cases with identical body in `view_glyph`
`SmartView::Blocked` and `SmartView::Review` both `return EmptyGlyph::Check;` as separate cases.
```
case SmartView::Blocked: return EmptyGlyph::Check;
case SmartView::Review:  return EmptyGlyph::Check;
```
Safe because: identical bodies — C++ case fall-through collapses them with no behavior change.
Replacement: `case SmartView::Blocked: case SmartView::Review: return EmptyGlyph::Check;`
**-1 line. Confidence: HIGH.**

### 5. src/ecs/loader_system.h:135 — redundant-guard `!app.sessions.empty()` already guaranteed
Inside the disk-cache prime branch we only enter after `cached && !cached->empty()` (line 129) and
`app.sessions = std::move(*cached)` (line 131) — so `app.sessions` is provably non-empty two lines
later.
```
if (auto cached = …; cached && !cached->empty()) {
    app.sessions = std::move(*cached);       // now >= 1 element
    …
    if (app.selectedId.empty() && !app.sessions.empty())   // 2nd term always true
```
Safe because: the `!cached->empty()` guard + the move guarantee ≥1 session. (Note: the identical
line at :165 is NOT redundant — there `sessions = move(r.value)` may be empty.) Replacement:
`if (app.selectedId.empty())`. **-1 term. Confidence: HIGH.**

### 6. src/ecs/main_pane_system.h:3909-3911 — dup `case SyncState::None:` duplicates `default:`
The sync-badge switch explicitly handles LocalOnly/Persisting/Synced/Failed; the tail is
`case api::SyncState::None: default: break;`. `None` shares the `default` empty body.
```
case api::SyncState::None:
default:
    break;
```
Safe because: all 5 enum variants are either named above or caught by `default`; removing the
`None` label leaves it caught by `default` — no behavior change. Replacement: drop
`case api::SyncState::None:`. **-1 line. Confidence: HIGH.**
(Bonus, out-of-scope non-branch: :3902-3903 `const theme::Color amber = theme::tag_ready_fg(); (void)amber;`
is a dead local — flag for the parent's non-branch sweep.)

### 7. src/api/mock_client.h:85-87 — redundant `else` re-sets a field to its default value
`get_session(id, limit)` calls `get_session(id)`, which returns a FRESH `Session` (default
`has_more_older = false`, types.h:170). The windowing sets it `true` only when it truncates; the
`else { r.value.has_more_older = false; }` re-asserts the already-default value.
```
if (msgs.size() > limit) { …erase…; r.value.has_more_older = true; }
else { r.value.has_more_older = false; }   // already false on a fresh Session
```
Safe because: the value defaults to false and `get_session(id)` never sets it true on stored/seed
sessions. Replacement: drop the `else`. **-2 lines. Confidence: MED** (depends on the invariant
that stored sessions never carry `has_more_older=true`; true today).

### 8. src/ecs/main_pane_system.h:395 (helper) — shrink `sending_for` OR-chain of early returns
`AppComponent::sending_for` is four `if (flag && id-match) return true;` lines then `return false;`
— a boolean-OR expressed as a branch ladder.
```
if (sendPending && sendSessionId == id) return true;
if (steerPending && steerSessionId == id) return true;
if (streamCollecting && streamPendingSession == id) return true;
if (streamActive && streamSessionId == id) return true;
return false;
```
Safe because: pure short-circuit OR — identical semantics as a single `return (…||…||…||…);`.
Replacement: one `return` with `||`-joined terms. **-4 lines. Confidence: HIGH** (behavior
identical; borderline on the style/branch line, but it IS a branch ladder gating one bool).

### 9. src/ecs/sidebar_system.h:445-446 — dup `case Glyph::Automated` body equals the `default`
In `glyph_color`, `case Glyph::Automated: return theme::text_faint();` returns exactly what
`default:` returns. Since `draw_glyph` returns early on `Glyph::None` BEFORE calling `glyph_color`
(line 466), the only value that would hit `default` is None — which never arrives — so `default`
exists purely for exhaustiveness and duplicates the Automated arm.
```
case Glyph::Automated: return theme::text_faint();   // cron/quiet
default: return theme::text_faint();
```
Safe because: identical return; folding the explicit `Automated` case into `default` preserves the
color. Replacement: drop `case Glyph::Automated:` (let it fall to `default`). **-1 line.
Confidence: MED** (keeps a compiler-exhaustive default; the explicit case is only documentation).

### 10. src/ecs/settings_system.h:1003-1005 — yagni terminal `else` for a state that the ladder's own guards make unreachable-in-practice
`render_account_row`'s chain: `Loading → Error → settings.ok → else "not available"`. The final
`else` is the only genuinely-needed fallthrough and is fine AS a fallback — BUT note the branch is
reached only with `settingsState != Loading/Error && !settings.ok`, which for the mock/no-settings
backend is the steady state. This is a legitimate fallback; listed as LOW because the honest cut is
NONE here — flagged so the parent doesn't waste a pass on it.
Safe to remove? NO. **Confidence: LOW — keep.** (Included per "prove or mark low" rule: I checked
it and it is a real fallback, not dead.)

### 11. src/ecs/main_pane_system.h:778-782 — shrink redundant outer parens + enumerated HR lengths
The horizontal-rule branch wraps its whole condition in an extra paren pair and enumerates
`"---" | "----" | "-----"` (3/4/5 dashes) but only `"***"`/`"___"` at length 3.
```
} else if ((rest == "---" || rest == "***" || rest == "___" ||
            rest == "----" || rest == "-----")) {
```
Safe subset: the OUTER redundant parens around the `else if` condition can be dropped (pure
syntax). Removing the extra-dash enumerations would CHANGE behavior (6+ dashes stop matching), so
that part is NOT safe. Replacement: strip the outer `( … )`. **-0 lines (1 char pair); Confidence:
HIGH but trivial.** Listed low-value.

### 12. src/api/http_client.cpp:596-597 — CHECKED & REJECTED (documented so the parent doesn't re-flag)
The empty `else if (bt == cfg.field_block_tool_result_type) { /* consumed via find_result */ }`
LOOKS deletable but is NOT: the final `else` (line 619) surfaces text for "any OTHER block type."
Deleting the empty branch would let tool_result blocks fall into that `else` and get their content
appended to the assistant text — a regression (double-rendered tool output). **Keep. Confidence:
HIGH that this must stay.**

---

net: -18 lines possible across 8 actionable findings (F1–F9, F11 minus the keep-only F10/F12)

Highest-value / lowest-risk to do FIRST:
- **F2** (main.cpp blocked-notify dup, ~-4 lines, HIGH) and **F8** (`sending_for` OR-collapse,
  -4 lines, HIGH) are the biggest safe cuts — both pure behavior-preserving simplifications.
- **F1, F3, F4, F5, F6** are one-liner dead/duplicate-branch removals with airtight proofs (const
  guard, enum-default coverage, default-value invariant) — trivial, zero-risk, do them in one sweep.
