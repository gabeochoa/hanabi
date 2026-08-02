# Local-first ideas for hanabi (grounded in Ink & Switch, 2019)

## The honest constraint
hanabi can't be *fully* local-first: the agent runs online (server is the authority
that DOES the work), and the UI is mostly an API wrapper. CRDTs / multi-device
concurrent editing don't apply — there's no local document the user co-edits; the
server is the single writer of transcript state. BUT the article's core move —
"treat the local copy as the PRIMARY copy for reading + authoring, server as a
secondary/sync role" — applies cleanly to hanabi's THREE local surfaces:
READ (browse threads), AUTHOR (your prompts), NAVIGATE (search/history). Lean in there.

## 5 ideas (mapped to the article's 7 ideals)

### 1. Local copy is the PRIMARY read copy — stale-while-revalidate (ideals #1 no spinners, #3 network optional)
We already cache transcripts (LRU + disk). Go further: on open, render INSTANTLY
from the local disk copy every time, then revalidate in the background and diff in
updates — never block the UI on a network fetch to READ. Full offline browse of
everything you've ever opened. The agent must be online to WORK, but you should
never wait (or see a spinner) to READ your own threads. Biggest, most-achievable win.
Status: partially built (transcriptCache + disk_cache); make it the DEFAULT read path.

### 2. Local append-only outbox for prompts/steering — never lose typed work (ideals #1, #7 ownership)
Just shipped draft persistence (drafts.json). Extend it to an append-only local
OUTBOX: every prompt, steering message, and queued send is written to disk BEFORE
it touches the network. A crash / quit / offline never loses a keystroke; on
reconnect the outbox drains to the API. This is "primary copy is local" applied to
the one thing hanabi actually AUTHORS — your words. (Directly extends the draft work.)

### 3. Local full-text search index over cached transcripts (ideals #1 at-your-fingertips, #7)
Cloud search is server-bound + was flaky (Gabe's prior "search is broken" pain).
Build a local index (SQLite FTS5, or a simple on-disk inverted index) over cached
transcripts. Instant, offline search of your history with zero round-trips. Pure
local-first: your data, your index, searchable even if the backend is down.

### 4. Owned, durable export — "the Long Now" (ideals #5 longevity, #7 ownership)
Transcripts are already cached as JSON locally. Add one-command export to standard,
durable formats (Markdown + JSON per thread) into a user-owned folder
(e.g. ~/hanabi/threads/*.md), optionally auto-mirroring every viewed thread. Your
conversations survive even if the backend is sunset — "no Wayback Machine can
restore a Google Doc," but a local .md folder lasts. Cheapest big win: data is
already local, just make it OWNED + portable + human-readable.

### 5. Optimistic offline send + local pending state (ideals #3 network optional, #4 collaboration)
When offline or a send fails, DON'T error — accept the prompt into the local queue,
show it optimistically in the transcript as "pending · will send when online," and
drain when connectivity returns. Reframes the composer from "requires server" to
"local-first with background sync": draft + queue on a plane, it sends when you land.
(Builds on #2's outbox + the existing message queue.)

## Suggested sequencing (leverage what exists)
#2 (outbox) and #5 (offline send) are natural extensions of the draft-persistence +
message-queue we just built → do next. #1 (stale-while-revalidate read) hardens the
cache we already have. #4 (export) is a small, high-ownership win. #3 (local search)
is the biggest new build but fixes a real long-standing pain.
