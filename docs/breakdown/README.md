# Feature breakdowns

Seven documents turning `puffin_gaps.md` into commit-sized work. Each was
written by a separate agent whose FIRST job was to check hanabi's source before
planning anything, because the gap doc's claims about our own code proved
unreliable.

## Read this before picking up work

**Roughly a third of the "hanabi is missing X" claims were false.** Across the
areas below, 78 claims were examined and 22 turned out to be already shipped. That is the most useful output here — the real remaining work is
noticeably smaller than the 79-gap headline suggests.

Still: **grep before you build.** Two claims survived an agent's own check and
were caught on review afterwards. Both had hedged their own wording, which is
worth treating as a signal rather than a footnote.

| document | area | examined | already built | real gaps |
|---|---|---|---|---|
| `session-lifecycle.md` | rename, fork, archive, delete, mute | 7 | 2 partial | 4 + 1 blocked |
| `composer.md` | history walk, slash commands, pickers, drafts | 13 | 5 + 1 partial | 7 |
| `transcript.md` | timestamps, thinking rows, highlighting, tables | 14 | 5 | 9 |
| `sidebar-tabs.md` | sidebar nav, tabs, windows | 15 | 2 + 1 partial | 12 |
| `search-settings-shortcuts.md` | cross-session search, settings, keys | 18 | 2 | 16 |
| `native-notifications-attachments.md` | notifications, macOS, attachments | 11 | 5 | 6 |
| `screenshot-testing.md` | visual regression harness | n/a | n/a | 1 workstream |

## Overlaps

Agents worked in parallel and did not see each other's output, so three things
were written up twice. Each now names one owner:

- **Session rename** — `session-lifecycle.md` owns it (it verified the backend
  verb is advertised on attach). `sidebar-tabs.md` keeps only the entry point.
- **Composer history walk** — `composer.md` owns it. `search-settings-shortcuts.md`
  had it bundled with find-next/prev; that bundle is now find-only.
- **Global hotkey** — not a contradiction but it read like one.
  `native-notifications-attachments.md` verified a focus-gated Carbon hotkey
  already exists; `search-settings-shortcuts.md` wants a SECOND chord for the
  palette. That makes it small, not medium — the focus-gating is the hard part
  and it is done.

## Corrections already applied

- `composer.md` listed streaming "working dots" as built on the reasoning
  "implied by streaming support". Nothing renders one; it is a real gap.
- `composer.md` listed the token meter as built. The proportion bar exists but
  only draws when a context window is configured, which nothing sets on the
  current backend — so in practice it is always the plain figure. The real
  denominator is queued in `todo.md`; do not re-plan it.
- `screenshot-testing.md` reached the right verdict on determinism by the wrong
  route. See the note there: stability comes from the mock seeding timestamps
  relative to now, not from the clock holding still.

## Constraints every document was given

- Behaviour and UX flow, not a SwiftUI-to-C++ translation. The reference client
  is a different stack; the shapes do not transfer.
- One theme per chunk, roughly 50-250 lines, each independently shippable and
  reviewable.
- Every chunk says how it is proven — a scripted UI test, a unit test, a
  screenshot, or honestly "manual", for the things the harness cannot reach.
- Nothing internal in the text: no company names, hosts, service names or
  internal paths. Scanned before commit.
