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
| Index | none | rebuilt on every open | none |
| Cost | file read per session, first frame of a query | **full JSON parse of the whole disk cache, on the UI thread** | full re-scan **every frame** |
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

**Order** is `app.sessions` order. The header comment says the caller adds them
newest-first (`session_index.h:139`); see **S7**.

**Presentation.** Six rows, a 32-byte-context snippet, and a coverage sentence
— *"Full text for 4 of 61 threads; the rest by title and preview only"* — which
is the best idea in the whole search story and is also wrong twice (**S2**,
**S4**).

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

**Cost.** Nothing is cached. Every frame the bar is open, every loaded message
is re-scanned: two whole-string allocations per message (`redact_secrets`,
`strip_inline_md`) plus `md_to_spans` per line (`:3387-3395`). No file I/O.

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

### S2 — "Full text" is not full text for the threads you most recently read

`transcript_cache.h:108` caps the in-memory copy at the **last 20 messages**.
`build_index` prefers that copy over the disk one (`:302`) and stamps it
`Depth::Full`, so `coverage_note` says *"Full text for all N threads"* while
the five threads you were just reading were indexed 20 messages deep.

The comment justifying the preference — *"it is the newer of the two"* — is
true and irrelevant. Newer is not fuller. This defeats the file's own stated
purpose: *"A search that quietly misses half your history and reports '3
results' is the failure mode this file exists to avoid"* (`session_index.h:22`).

**What it would take: small.** Prefer disk when the cached copy is at the cap,
or add a third `Depth::Windowed` and say so in the note.

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

### S5 — opening Cmd+Shift+F parses the entire disk cache on the UI thread

`build_index` (`:295`) runs `load_transcript` — a full JSON parse — for every
session not in the LRU, synchronously, on the frame the panel opens. No cap, no
budget, no async, no progress. The repo's own figure for a merely *stat*-based
walk of 2000 cache files is 5.9 ms (`disk_cache.cpp:222`); this is a parse per
file. And "once per opening" is undercut by both `close()` and the chord
resetting `indexed_`, so it is once per open, every open.

**What it would take: medium.** Cap the number of transcripts read, or build
off-thread. The coverage note is already the right place to admit a partial
index, which makes this cheaper than it looks.

### S6 — the index is never freed — FIXED HERE

`index_` is a system member holding the corpus **twice** (the docs and a
lowercased copy, `session_index.h:122`). `close()` cleared the query and the
flag and not the index, so one Cmd+Shift+F on a large cache permanently doubled
the app's transcript footprint. Fixed: `close()` releases it.

### S7 — result order is "whatever the server sent", documented as newest-first

`session_index.h:139` says the caller adds them newest-first. `build_index`
iterates `app.sessions` unmodified; `app.sessions` comes straight from the
adapter (`loader_system.h:200`); `MockClient::list_sessions` sorts, but
`HttpClient::list_sessions` and `AgentcloudClient::list_sessions` push rows in
wire order. The sidebar sorts its own copy precisely because it cannot trust
the incoming order — the codebase disproving its own comment.

**What it would take: one line.** Sort the hits by `updated_at`.

### S8 — "Show N more…" on a search result list un-virtualizes it

`row_window` bails out whenever a query is live (`:2364`), on the argument that
`visible_limit` caps the list at two viewports. `visible_limit` returns the
whole total once `__more_<key>__` is in `collapsedFolders` (`:2308`). Compose
the two — search, scroll, click `sb_show_more` — and every matched row is built
every frame, which is the exact defect
`sidebar_show_all_is_still_virtualized.e2e` exists to prevent. That test never
types a query; `search_does_not_draw_the_whole_catalog.e2e` never clicks the
expander. The combination is untested.

**What it would take: small.** Window with the taller search-row pitch instead
of skipping, or refuse to uncap while a query is live.

### S9 — three things nobody is told

- **The sidebar truncates silently.** `visible_limit`'s justification is *"the
  count in the header is still the true number of matches"* (`:2300`) — but the
  catch-all group is headerless (`:2663`), so there is no header and no count.
  `Show N more…` exists, two viewports below the fold.
- **Cmd+Shift+F caps at six and says nothing about it.** `kMaxRows = 6` is both
  the query limit and the row limit; the note reports depth, never breadth. A
  query hitting two hundred threads shows six rows and a sentence about
  something else. **One-line-ish fix:** query for seven, render six, append
  "showing 6 of 200+".
- **Cmd+F never says the transcript is windowed.** `app.hasMoreOlder` is right
  there and drives a load-older trigger; the bar still just says "no matches".

### S10 — two tests that pass for the wrong reason

- **`sidebar_search_snippet.e2e`** types `workers` and asserts the snippet
  `1 of 3 workers has reported`. That string is session `r6`'s **preview**, and
  the row matched on its **title**, which also contains "workers". `r6` is not
  in the LRU, so `snippet_for`'s transcript branch — the feature the commit
  sells, *"a matching row now carries the line the match is on"* — never runs.
  Delete that branch and the test stays green.
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

### S12 — a multi-word query that straddles a wrap is counted and not painted

`collect_matches` scans the **logical** line; `paint_bands` scans each
**wrapped** line, and the wrapper consumes the whitespace at the break
(`vendor/afterhours/.../text_selection.h:184`), so `"6 failures"` split across
two rendered lines exists in neither. Independent of S1 and it breaks the same
rule. `find_sees_through_markdown.e2e` uses exactly such a query at a window
width where it happens not to wrap.

**What it would take: small.** Count over the wrapped lines — which is what
"count what you paint" actually means.

### S13 — smaller things

- **Two snippet cutters** with different context widths (32 in
  `session_index.h:77`, 22 in `snippet_text.h:27`) and different whitespace
  handling, sharing the same "don't start mid-word" trick and the same bug in
  it: the trim only fires if whitespace exists between the window start and the
  match, so a hit inside a long token still yields the `…imization` the comment
  says it prevents. Both cut at raw byte offsets with no UTF-8 boundary check.
- **The sidebar reads the disk cache on a backend where caching is off.**
  `content_matches` is called unconditionally (`:2468`); writes are gated on
  `backend_label != "mock"` (`loader_system.h:37`). The mock's cache dir is the
  same flat directory an http backend with an empty base URL writes to, so the
  mock's sidebar can match files another backend left behind.
- **No Unicode anywhere.** All four matchers fold `A-Z` only; nothing
  normalizes. `Café` does not match `café`. The locale-dependent
  `std::tolower` in `disk_cache.cpp` is gone with **S3**; the remaining three
  were already ASCII.
- **`find_nav::advance(i, n, Step::None)` returns 0, not `i`** — asserted as
  intended in `test_find_nav.cpp:62`, harmless because the only caller
  short-circuits first, and a footgun for the second one.

---

## 6. The shortest version

The find bar is the best-built of the three and rested on an invariant that is
false as soon as the thread is longer than the screen; the invariant is now the
true one and the culled case is tested (**S1**). The
cross-session panel has the right instinct — it tells you what it could not
see — and then lies in the sentence that does it (**S2**), while parsing your
whole cache on the UI thread to get there (**S5**). The sidebar's deep search
searched the JSON document rather than the conversation (**S3**, fixed). None
of the
three tells you when it truncated (**S9**), and the two tests that would have
caught the two most embarrassing of these pass without the code they name
(**S10**).
