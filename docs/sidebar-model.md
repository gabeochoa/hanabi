# Hanabi — thread organization model

Core principle: **surface what needs the user; keep what's self-running quiet.**
"Unread"/attention means a thread is DONE or WAITING ON THE USER. Nothing else.

## Smart views (top of sidebar)
1. **Home** — a digest, not a dashboard: a numbered list of open decisions,
   ordered (a) Waiting on you → (b) Finished since you last looked → (c) Self-running (collapsed).
   No-change updates are suppressed. Goal: rarely need to open individual threads.
2. **Blocked** — only threads where the sole remaining gate is a human action only the user
   can take: a decision, an approval (publish/land/send), or a review the user must give.
   HIGH bar. Transient failures, retries, and self-recoverable states never appear here.
3. **Ready to test** — flips only when checks pass, review comments are addressed, and real
   before/after evidence exists. Includes a live test link + steps when applicable.
4. **Starred** — user-pinned, pinned to top.

## Attention / signal rules
- A row shows an unread dot + **bold** title only when DONE or WAITING-ON-YOU.
- Self-running threads are dimmed and quiet: no dot, no bold, no "still running" nudge.
- Tags: BLOCKED, DONE, READY — one per row, only when relevant.
- The rail attention badge counts BLOCKED only.
- **Parked/muted** state: never nudges, never counts.

## Folders
- User-defined grouping. A thread can be starred AND filed in a folder.
- Folders are collapsible and show a plain count.

## Voice for any surfaced text
- Terse, one thought per line, no filler.
