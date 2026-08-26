# Search

There is no such thing as "search" in hanabi. There are three unrelated
features with three different corpora, three different notions of a match, and
two different answers to "what does an empty query mean". They share no code
except a lowercase helper, and one of them hands its query to another across a
seam where the two disagree about what is searchable.

This is the map, and then the honest part.

| | **Sidebar filter** | **Cmd+Shift+F** | **Cmd+F** |
|---|---|---|---|
| What it is | filters the thread LIST | finds threads by content | finds text in the OPEN thread |
| How you get in | click the pill; no keybinding, no palette entry | Cmd+Shift+F, or the palette | Cmd+F; Cmd+G / Shift+Cmd+G to step |
| Unit of result | a session row | a session row | a highlighted span |
| Empty query | **everything** | **nothing** | nothing |
| Operators | none (literal) | none (literal) | `is:` `has:` `state:` |
| Corpus | title + the cached transcript's message text | title + preview + user/assistant text | the **painted** text of the open thread |
| Thinking blocks | searched | searched | skipped |
| Tool rows | searched (their `text`) | skipped | skipped |
| Tool OUTPUT | never (not persisted) | never | never |
| Markdown markers | present | present | stripped |
| Code fences / tables | present | present | skipped |
| Index | none | rebuilt on every open, a few threads a frame | none |
| Cost | **165 ms** first frame of a new query, 2000 threads (#368) | 0 disk reads to open; 8 transcripts a frame after (#367) | **+2.65 ms/frame**, doubling it (#365) |
| Cap | ~2 viewports of rows | 6 results | none |
| Says when it truncated | no | no | n/a |

Everything below cited `file:line` at `f012198`. Entries marked FIXED HERE
have been changed since and cite by name instead — a line number in a file this
branch edited is a number that has already moved.

---

## 1. The sidebar filter

`src/ecs/sidebar_system.h`

**Getting in.** A click on the `sb_search` pill at the top of the unfolded
sidebar (`:1683`). There is no shortcut, no palette action, and no Escape
handler — `escape_system.h:38-55` has an intent for the palette, the
cross-session panel, rename, the composer, the shortcut sheet, settings, find,
the slash menu and the model picker, and none for this box. The only way to
clear it is the `✕` at `:1854`. Folding the sidebar skips the whole block
(`:184-190`), so a live query survives invisibly across a fold.

**The match.** Two predicates, ORed, per session per frame (`:2466`):

```cpp
if (match &&
    (title_matches(s.title, q) ||
     (!q.empty() && api::disk_cache::content_matches(s.id, q))))
```

`title_matches` (`:813`) is `lower(title).find(q) != npos` — a byte substring,
ASCII-only case folding (`util/format.h:19-23`), no tokens, no fuzzy. Empty
query returns true, which is why an empty box shows everything.

`content_matches` does **not** search the conversation. It searches the raw
cache file (`api/disk_cache.cpp:472`):

```cpp
std::string blob((std::istreambuf_iterator<char>(in)), ...);
for (char& c : blob) c = std::tolower(...);
return blob.find(lowerQuery) != std::string::npos;
```

That file is `{"version":1,"summary":{…},"messages":[…],"sub_agents":[…],…}`
(`disk_cache.cpp:307-321`). See defect **S3**.

**Caching.** A `(query, corpus-generation)` memo (`disk_cache.cpp:465-517`),
with a narrowing shortcut: typing another character re-reads only the sessions
that matched the shorter query. The generation is bumped by `set_namespace`,
`save_transcript`, `wipe_all` and `trim_to_cap`.

**Order and presentation.** Not by relevance: `updated_at` desc, then id, then
hand-dragged pins float (`:2497-2530`). Each surviving row grows a snippet
(`snippet_for`, `:2866`) — the first line of a user/assistant message
containing the query, if the thread happens to be in the in-memory LRU;
otherwise a cut of the preview; otherwise the whole preview, with nothing lit.

**Virtualization is off while searching** (`:2357-2365`): `row_window` returns
the full range whenever a query is live, on the argument that `visible_limit`
already caps the list at two viewports. See **S8**.

---

## 2. Cross-session search — Cmd+Shift+F

`src/ecs/session_search_system.h`, `src/search/session_index.h`

**Getting in.** `Cmd+Shift+F` (`:54`) toggles, clearing the query and dropping
the index. The palette's "Search threads" (`palette_system.h:281`) opens it
without clearing either. Escape closes, arrows move, Enter opens.

**The index.** Built on the frame the panel opens (`:81`):

```cpp
if (const api::Session* held = app.transcriptCache.peek(s.id)) {
    d.body = flatten(*held);  d.depth = Depth::Full;
} else if (auto disk = api::disk_cache::load_transcript(s.id)) {
    d.body = flatten(*disk);  d.depth = Depth::Full;
}
```

`load_transcript` is a full nlohmann parse (`disk_cache.cpp:331`). There is no
cap, no budget, and no thread. See **S5**, **S2** and **S6**.

`flatten` (`:320`) keeps user and assistant text, raw — markdown markers
intact, no secret redaction, thinking blocks included (a thinking block is an
assistant message with `subtitle == "thinking"`). Tool rows are dropped.

**The match** (`session_index.h:143`) is an ASCII-case-insensitive byte
substring against a pre-lowered copy, tried body → title → preview, first hit
wins. Empty query returns nothing, deliberately: *"an empty query matches
nothing, because 'everything' is not a search result"* (`:141`) — the exact
opposite of the sidebar box eight files away.

The query runs **every frame the panel is open** (`:86`), not per keystroke.

**Order** is newest-thread first, ties broken by id (`session_corpus.h`,
`begin`). It used to be `app.sessions` order — see **S7**.

**Presentation.** Six rows, a 32-byte-context snippet, a line saying there were
more when there were (**S9**), and a coverage sentence — *"Full text for 4 of
61 threads; the rest by title and preview only"* — which is the best idea in
the whole search story and used to be wrong twice (**S2**, **S4**).

---

## 3. Find in conversation — Cmd+F

`src/ecs/main_pane_system.h`, `src/ui/find_*.h`

**Getting in.** `Cmd+F` without Shift (`:98`), which is what keeps it disjoint
from Cmd+Shift+F. `Cmd+G` / `Shift+Cmd+G` step; the chord's whole meaning is a
four-line table in `ui/find_nav.h:21-37` because a scripted test cannot press a
Cmd chord (gap #49).

**The corpus is what is painted, not what is stored.** `paintable_lines`
(`:2835`) runs `redact_secrets` then `strip_inline_md`, and for assistant rows
walks the body exactly as the renderer does — so fenced code blocks and
markdown tables are skipped, headings contribute their text, and everything
else contributes `md_to_spans(line).visible`. Tool rows, System captions and
thinking blocks are excluded by role and subtitle (`:2916`).

This is the design rule the whole feature is built on, and it used to be
stated in four separate files as **the tally equals the bands**, which is false
as soon as the thread is longer than the screen. It now reads **nothing is
counted that find could not paint** — `find_highlight.h`, `find_operators.h`,
`snippet_highlight.h`, `main_pane_system.h` (`collect_matches`), and the test,
renamed `find_counts_only_what_it_could_paint.e2e`. See **S1**.

**Operators** (`ui/find_operators.h`). A token is an operator only if the text
before its first `:` is exactly `is`, `has` or `state`; `http://x` and
`foo:bar` stay plain text (`:105`).

| Term | Values | Means |
|---|---|---|
| `is:` | `user` | the row is a user message |
| `is:` | `assistant`, `agent` | the row is an assistant message |
| `has:` | `tool` | the row's **turn** contains a tool message |
| `state:` | `failed`/`error`, `completed`/`success`/`ok`, `running`/`in_progress` | some tool in the turn is in that state |

Terms are ANDed with each other and with the plain text. An unknown key stays
plain text; an unknown *value*, or a bare `is:`, sets `invalid` and the bar
renders `Try: is:user, is:assistant, has:tool, state:failed`. A well-formed
operator with no plain text reports "no matches" with no hint (**S9**).

**Cost. Measured: the find bar doubles the frame.** Nothing is cached. Every
frame the bar is open, every loaded message is re-scanned: two whole-string
allocations per message (`redact_secrets`, `strip_inline_md`) plus
`md_to_spans` per line. No file I/O.

`HANABI_PROF=1 HANABI_SOAK=600`, 1180×949, the 480-message fixture (40 loaded),
CPU time — two new profiler scopes, because this path was invisible to the
profiler and that is part of why it went unnoticed:

| | bar closed | bar open |
|---|---|---|
| `FRAME (cpu)` | 2.807 ms/f | **5.452 ms/f** |
| `find.collect` | — | 2.430 ms/f (45% of the frame) |
| `find.paint` | — | 0.341 ms/f |
| allocations | 2660 /f | 14788 /f |

It is a LEVEL, not a slope, so `soak-gate` reads it as flat, `scaling-gate`
never opens the bar, and none of `alloc-gate`'s three fixtures does either.
`afterhours_gaps.md` #365: the fix is a per-message memo of `paintable_lines`,
half a day, and the app already has three caches of exactly that shape.

---

## 4. Where the three contradict each other

1. **`is:user` means three different things.** A literal substring in the
   sidebar. A literal substring in Cmd+Shift+F. A role filter in Cmd+F — and
   typed alone it reports "no matches", because the plain-text part is empty.

2. **The Cmd+Shift+F → Cmd+F handoff can land on "no matches".** Selecting a
   result sets `findQuery` to the same string (`session_search_system.h:269`).
   But the hit was found in `flatten()`'s corpus, which contains thinking
   blocks, raw markdown markers, unredacted secrets and code-fence bodies — all
   four of which `collect_matches` refuses. The commit message for
   `feat/cross-session-search` claims the opposite: *"the match is highlighted
   and counted by the machinery whose tally already equals the bands it
   paints."* That sentence was wrong twice over — see **S1** for the tally, and
   this entry for the corpus mismatch, which is still open.

3. **The sidebar matches JSON.** Type `state` and every thread with a cached
   transcript matches, because `"state"` is a key in the summary object.

---

## 5. What is wrong with it

Ranked by how much it matters. Everything here was read out of the code; the
two marked SUSPECT were not run.

### S1 — the tally counts matches the app does not paint — DECIDED AND FIXED HERE

`main_pane_system.h` sets `findCount` from every **loaded** message. Bands are
painted only inside the virtualization window, and again per segment inside
`render_rich_body`. So `bands ≤ count`, with equality only when the thread fits
the window.

**The decision: the count is right and the rule was wrong.** A reader searching
a thread wants to know how many matches are in the thread — "3 of 47" means 47
in the document in every editor there is, and the chevrons are how you reach
the 44 that are not in front of you. Making the tally scroll-dependent would
have made the number change under a scrollbar for no reason a user could name.

So the count is unchanged and the claim is rewritten. The invariant that is
actually true, and is now what those files say, runs the other way: **nothing
is counted that find could not paint** — same rows, same normalization, same
operator predicate on both sides. `bands ≤ count` is a fact about the window,
not a violation.

The test is real now. It was one line, `expect_text "no matches"`, satisfied by
any bug returning zero, and it never enabled the band audit it was named after:

- `find_counts_only_what_it_could_paint.e2e` (renamed) asserts `bands 0`
  alongside the zero. Deleting the code-fence skip from the PAINTER alone —
  leaving the count at zero — now fails on `bands 0` and passes the old line.
- `find_counts_a_paintable_match.e2e` (new) is the control: same thread, same
  fence, `proration` outside it, `1 of 2` and `bands 2`. Making
  `collect_matches` return nothing fails this and leaves the zero test green,
  which is precisely how the zero test passed against broken code.
- `find_counts_the_thread_not_the_window.e2e` (new) is the culled case the
  suite never had: a 57-line reply, folding off, in a 760px window. `1 of 56`
  with `bands 36` at the bottom and `bands 31` at the top — the tally holds
  still across a `HOME`, the bands do not. Implementing the other decision
  (`findCount = band_count()`) fails it at both ends and breaks nothing else.

Still counted-but-unpaintable, and tracked separately: a multi-word query
straddling a soft wrap (**S12**), and a match inside the folded tail of a long
message (which is at least reachable by a click on the fold).

### S2 — "Full text" is not full text for the threads you most recently read — FIXED HERE

`transcript_cache.h` caps the in-memory copy at the **last 20 messages**.
`build_index` preferred that copy over the disk one and stamped it
`Depth::Full`, so `coverage_note` said *"Full text for all N threads"* while
the five threads you were just reading were indexed 20 messages deep — the
shallowest entries in the corpus, reported as the deepest.

The comment justifying the preference — *"it is the newer of the two"* — is
true and irrelevant. Newer is not fuller.

Fixed both ways the entry suggested, because they answer different halves:

- `TranscriptCache` now records whether an entry was **cut** on the way in
  (`truncated(id)`; a size check cannot answer it, since a thread with exactly
  20 messages is complete). `build_index` prefers the LRU only while it holds
  the whole thread, and reads the disk copy when it does not — whichever holds
  more messages wins.
- `Depth::Windowed` is the third state, for a body that is a tail: cut into the
  cache, or fetched with `has_more_older`. `coverage_note` grew a clause for it
  — *"Full text for 0 of 21 threads; 1 to their newest messages only; the rest
  by title and preview only"* — and the result row is marked
  *"(recent messages only)"*, distinct from *"(title and preview only)"*,
  because an absent match in a tail is not evidence that the word was never
  said.

`session_search_says_what_it_only_skimmed.e2e` drives it on the 480-message
fixture, where the searchable copy is the last 20 of 480, plus a unit test on
the three-way depth and every branch of the note's wording.

### S3 — the sidebar's "content" search greps JSON structure — FIXED HERE

`disk_cache.cpp` lowercased the whole file and called `find`. So `state`,
`tag`, `preview`, `subtitle`, `version`, `folder`, `starred`, `created`,
`messages`, `has_more_older` and every session id matched every thread with a
cached transcript. Title matching runs first, so it only fired once the title
missed — which is what made it look like a legitimate deep hit.

Fixed with `src/search/json_field_scan.h`: one pass over the same bytes that
knows just enough JSON to tell a key from a value, and looks inside the values
of `text` and nowhere else. Not a parser — no tree, no validation — so it costs
what the lowercase-and-find cost, and the memo absorbs it exactly as before.
String values are decoded as they are read, so a query with a quote or a
newline in it matches the body rather than the escape sequence, and the fold is
the ASCII one the other three matchers use instead of the locale-dependent
`std::tolower` this had (part of **S13**).

The comment was also wrong about the corpus: *"message bodies and tool output
are all in there"*. `to_json(const Message&)` writes
`{id, role, kind, text, created_at, subtitle, tool_status}`. Tool output is not
in there and now the comment says so.

`test_content_search_matches_values_not_the_document` in
`tests/unit/test_data.cpp` pins it: seventeen assertions, every one of them
failing against the old scan.

### S4 — `state:` silently answers "no matches" on any restored thread — FIXED HERE

`tool_status` was not persisted, so a thread loaded from the disk cache (tab
restore, `main.cpp:300`; cache hit, `loader_system.h:285`) had `tool_status ==
""` for every tool row and no `state:` term could match. The query is *valid*,
so no hint rendered — it just reported zero.

Fixed: `tool_status` is now written and read back
(`disk_cache.cpp`), with a round-trip assertion in `tests/unit/test_data.cpp`.
`has:tool` was never affected — role is persisted.

`tool_result`, `tool_duration_ms`, `tool_node`, `image_path`, `sync` and
`run_outcome` are still dropped on the same path. Nothing reads them after a
restore today; `tool_result` is the one that would matter, because it is the
reason S3's comment claims tool output is searchable.

### S5 — opening Cmd+Shift+F parses the entire disk cache on the UI thread — FIXED HERE

`build_index` ran `load_transcript` — a full JSON parse — for every session not
in the LRU, synchronously, on the frame the panel opened. No cap, no budget, no
async, no progress. And "once per opening" is undercut by both `close()` and
the chord resetting `indexed_`, so it was once per open, every open.

**Measured** (`tools/bench_search_index.cpp`, 2000 threads × 40 messages =
19 MB of cache, `CLOCK_THREAD_CPUTIME_ID`, gabeochoa-mac):

| | before | after |
|---|---|---|
| opening the panel | **370.5 ms**, 2000 disk reads | **0.2 ms**, 0 disk reads |
| one frame after that | — | 1.1 ms, 8 disk reads |
| to full coverage | 370.5 ms, all in one frame | 324.3 ms over 250 frames |

The total work is the same; what changed is that no single frame pays it. The
reads are **spread**, not capped: `src/search/session_corpus.h` seeds the index
from the list rows alone (plus the in-memory bodies, which are free), and each
frame the panel is open reads `kDeepenPerFrame = 8` more transcripts,
newest-thread first. Coverage climbs while the query is still being typed, and
`coverage_note` — already the sentence that says how much was read — carries an
ellipsis until it is finished.

Rejected: a **cap** (Cmd+Shift+F would go permanently blind to old threads, and
a search that silently cannot see half your history is the exact failure
`session_index.h` opens by naming) and a **thread** (`api::disk_cache` has no
ownership story for a reader racing a save, and the whole corpus can be
invalidated under one — `save_transcript` bumps a generation the reader would
have to re-check per file).

The gate is a COUNT, in `tests/unit/test_session_index.cpp`: opening a
500-thread corpus performs zero loader calls, a frame performs exactly eight
whatever the catalog size, no thread is read twice, and it converges. A time
budget would read a different number of files on a loaded box than a quiet one
and could not be gated on a shared machine.

### S6 — the index is never freed — FIXED HERE

`index_` is a system member holding the corpus **twice** (the docs and a
lowercased copy, `session_index.h:122`). `close()` cleared the query and the
flag and not the index, so one Cmd+Shift+F on a large cache permanently doubled
the app's transcript footprint. Fixed: `close()` releases it.

### S7 — result order is "whatever the server sent", documented as newest-first — FIXED HERE

`session_index.h` said the caller adds them newest-first. `build_index`
iterated `app.sessions` unmodified; `app.sessions` comes straight from the
adapter; `MockClient::list_sessions` sorts, but `HttpClient::list_sessions` and
`AgentcloudClient::list_sessions` push rows in wire order. The sidebar sorts
its own copy precisely because it cannot trust the incoming order — the
codebase disproving its own comment.

Fixed in `CorpusBuilder::begin`, which sorts by `updated_at` descending with
`id` as the tie-break before anything is added. The tie-break is not cosmetic
here either: `updated_at` is a whole number of seconds, and the corpus is now
deepened over several frames (**S5**), so an unstable order would move results
under an arrow key between frames. `test_results_come_out_newest_first` feeds
it a wire-ordered list and a three-way tie.

### S8 — "Show N more…" on a search result list un-virtualizes it — STILL OPEN, `afterhours_gaps.md` #369

`row_window` bails out whenever a query is live (`:2364`), on the argument that
`visible_limit` caps the list at two viewports. `visible_limit` returns the
whole total once `__more_<key>__` is in `collapsedFolders` (`:2308`). Compose
the two — search, scroll, click `sb_show_more` — and every matched row is built
every frame, which is the exact defect
`sidebar_show_all_is_still_virtualized.e2e` exists to prevent. That test never
types a query; `search_does_not_draw_the_whole_catalog.e2e` never clicks the
expander. The combination is untested.

**What it would take: small, but not one line.** `row_window` bails because its
arithmetic is `offset / kRowHeight` and a search row is a different height;
windowing a variable-height list means knowing how tall a child would be, which
the library will not say (`afterhours_gaps.md` #224, and #326's `imm::vlist`
only virtualizes uniform rows). A searched list has exactly TWO heights, so the
app-side answer is to window with the taller pitch while a query is live, or
refuse to uncap while one is. Half a day, and the test has to do BOTH halves —
type a query AND click the expander — which is the test neither existing script
is. #369 carries the repo's own figure for the unvirtualized path.

### S9 — three things nobody is told — TWO OF THREE FIXED HERE

- **Cmd+Shift+F caps at six and says nothing about it** — FIXED. `kMaxRows = 6`
  was both the query limit and the row limit, so the note (which reports depth,
  never breadth) was the only thing under a list that could hit two hundred
  threads and look like six. It queries for seven now, renders six, and adds
  *"More matches — keep typing to narrow"* when the seventh came back. A true
  total was rejected: it means scanning every body to the end, every frame,
  over a corpus that can be tens of megabytes.
- **Cmd+F never says the transcript is windowed** — FIXED. Opening a thread
  fetches its newest 40 messages (`LoaderSystem::kMessagesWindow`), so "no
  matches" on a 480-message thread meant "not in the 40 we have" and read as
  "you never said that". `find_ops::bar_note` puts *"Older messages not
  loaded"* in the slot the operator hint uses; the hint wins when both apply,
  because a query the parser could not read makes the tally meaningless.
  Tested as a pure function, not a script: the transcript's own load-older
  PREFETCH fires while the UI harness settles and pulls the whole fixture in,
  so by the time a script can assert anything there is nothing left unloaded.
  The negative case — a whole thread stays quiet —
  is `find_is_quiet_on_a_whole_thread.e2e`.
- **The sidebar truncates silently** — STILL OPEN. `visible_limit`'s
  justification is *"the count in the header is still the true number of
  matches"* — but the catch-all group is headerless, so there is no header and
  no count. `Show N more…` exists, two viewports below the fold. See
  `afterhours_gaps.md` #372 for the shape of the fix and why it is not a
  one-liner.

### S10 — two tests that pass for the wrong reason

- **`sidebar_search_snippet.e2e`** typed `workers` and asserted the snippet
  `1 of 3 workers has reported`. That string is session `r6`'s **preview**, and
  the row matched on its **title**, which also contains "workers". `r6` is not
  in the LRU, so `snippet_for`'s transcript branch — the feature the commit
  sells, *"a matching row now carries the line the match is on"* — never ran.
  Deleting that branch left the test green. FIXED: the query is `before`, which
  is in exactly one title in the catalog (`t2`'s) and in `t2`'s first message
  but **not** in its preview — and `t2` is the restored tab, so its transcript
  is held. The asserted line can only come from the transcript.
- **`find_counts_only_what_it_paints.e2e`** was one line: `expect_text "no
  matches"`. Any bug that makes find return zero satisfied it, and it never set
  `HANABI_FIND_AUDIT=1`, so it never read the band count the file it is named
  after exists to expose. FIXED under **S1**: renamed, `bands 0` asserted, and
  given a control (`find_counts_a_paintable_match.e2e`).

### S11 — a comment claims a data-model limitation that does not exist — FIXED HERE

`find_operator_hint.e2e` and `todo.md` both said `is:thinking` is blocked
because *"the http adapter folds a thinking block's text into the assistant
text run, so nothing distinguishes it once it is stored"*. It does not:
`http_client.cpp:189` sets `subtitle` from the block type,
`agentcloud_client.cpp:511` sets `"thinking"` explicitly, and
`main_pane_system.h` tests exactly that in three places.
`find_operators.h:26-28` already corrects itself and gives the real reason —
a thinking row has no highlight path, so the operator could only ever answer
"no matches". The two stale copies now say that.

### S12 — a multi-word query that straddles a wrap is counted and not painted — FIXED HERE

`collect_matches` scans the **logical** line; `paint_bands` scanned each
**wrapped** line, and the wrapper consumes the whitespace at the break
(`vendor/afterhours/.../text_selection.h`), so a phrase split across two
rendered lines was in the logical line and in neither rendered one. Counted,
never painted, at any scroll position — the one class of match that breaks the
rule outright rather than because the message is off screen.

Fixed the other way round from the entry's suggestion. Counting over the
wrapped lines would have made the tally a function of the window width, so
resizing the window would change "of 47"; and it would have needed the layout
in the counting path, which has no rect and no font. Instead `paint_bands`
matches over the whole line and then MAPS each hit onto the wrapped ones,
painting a rectangle per line the match lands on and counting it once.

The mapping is reconstructed, not asked for, and it is exact rather than a
guess because of a property of the wrapper worth naming: it breaks only between
whitespace-separated chunks, never inside a word (a word wider than the line
gets a line to itself, uncut), and it never rewrites a byte — *"hard-broken
text round-trips byte for byte"*. So every wrapped line is a contiguous
substring of the original, in order, and `find()` from the previous line's end
locates it. `afterhours_gaps.md` #366 is the API that would make it stop being
a reconstruction.

`find_paints_a_match_that_wraps.e2e` drives it at a 1000px window, where the
break falls inside `The import` in `r2`'s last reply. `find_sees_through_
markdown.e2e` — the test the entry names — keeps its 1100px width, where the
same shape of query happens not to wrap.

### S13 — smaller things — ONE FIXED, THE REST WRITTEN UP AS `afterhours_gaps.md` #371

- **Two snippet cutters** with different context widths (32 in
  `session_index.h:77`, 22 in `snippet_text.h:27`) and different whitespace
  handling, sharing the same "don't start mid-word" trick and the same bug in
  it: the trim only fires if whitespace exists between the window start and the
  match, so a hit inside a long token still yields the `…imization` the comment
  says it prevents. Both cut at raw byte offsets with no UTF-8 boundary check.
- **The sidebar reads the disk cache on a backend where caching is off.**
  `content_matches` is called unconditionally; writes are gated on
  `backend_label != "mock"` (`loader_system.h:37`). The mock's cache dir is the
  same flat directory an http backend with an empty base URL writes to, so the
  mock's sidebar can match files another backend left behind. Not a one-line
  gate: `disk_cache` does not know about backends, so it wants an "is this
  cache live" flag set at startup rather than the predicate copied.
- **No Unicode anywhere.** All four matchers fold `A-Z` only; nothing
  normalizes. `Café` does not match `café`. The locale-dependent
  `std::tolower` in `disk_cache.cpp` is gone with **S3**; the remaining three
  were already ASCII.
- **`find_nav::advance(i, n, Step::None)` returns 0, not `i`** — asserted as
  intended in `test_find_nav.cpp:62`, harmless because the only caller
  short-circuits first, and a footgun for the second one.

---

## 6. The shortest version

The find bar is the best-built of the three and rested on an invariant that was
false as soon as the thread was longer than the screen; the invariant is the
true one now — *nothing is counted that find could not paint* — and the culled
case is tested (**S1**), the one match that broke it outright is painted
(**S12**), and the bar admits when the thread it searched is only its newest 40
messages (**S9**). The cross-session panel had the right instinct — it tells
you what it could not see — and lied in the sentence that does it (**S2**),
while parsing your whole cache on the UI thread to get there (**S5**, 370 ms at
2000 threads, now 0.2 ms); both fixed, and it says when there were more results
than fit (**S9**) and comes back newest-first (**S7**). The sidebar's deep
search searched the JSON document rather than the conversation (**S3**).

Still open, all written up with numbers in `afterhours_gaps.md`: the sidebar
truncates without saying so (**S9**, #372) and un-virtualizes when a search
meets "Show N more" (**S8**, #369); its deep filter costs 165 ms on the first
frame of a new query (#368); the find bar costs 2.43 ms a frame it does not
need to (#365); and four small things are #371.

And the two tests that would have caught the two most embarrassing of these
passed without the code they name (**S10**) — both fixed, one renamed, three
new scripts and five new unit tests around them.
