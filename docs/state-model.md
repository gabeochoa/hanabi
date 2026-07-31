# hanabi — thread state model (from real session analysis)

Derived from a read-only analysis of ~200 of the user's own threads. The user
already encodes state in TITLES; the client should parse those markers, not
rely on raw DB status or recency.

## Task-type clusters (rough share)
- Investigations / root-cause & bug triage (~25%) — largest bucket.
- Data queries / analytics (~15%).
- Code / diff work & product builds (~15%) — ends in a diff awaiting accept/land.
- Tooling / dev-env & self-built tools (~12%).
- Doc / post / comms writing (~10%) — produces a draft to review.
- Manager / people / perf & 1:1 (~10%) — long-lived per-person threads.
- Oncall / alert triage (~5%).
- Planning / roadmap / strategy (~5%).
- Monitoring / automation & cron (~3%).
- One-off Q&A / personal (~10%) — short, no follow-up.

## State enum (what the client models)
| state | when | attention | shape/treatment |
|---|---|---|---|
| needs_you | user is the gate: title `on <user>` / `awaiting approval` / un-actioned owned D-number / paused on a decision they own | ATTENTION | red up-triangle, bold title, top of Blocked |
| done | terminal marker (DONE/concluded/delivered), not yet archived | ATTENTION (once) | blue dot; unread until acknowledged, then demote |
| review | agent-verified, ready for the user to just look | ATTENTION | green diamond |
| working | active, agent progressing, NO user-gate marker | calm | no glyph, dimmed; never nudges |
| parked | `[P]` / deliberately shelved, gate external/future | calm | pin glyph, dim, collapsed |
| blocked_external | waiting on someone else (reviewer, privacy review, other team) | calm | grey "waiting" chip; a "Waiting on others" filter, NEVER Blocked |
| archived | user filed away | muted | hidden from default view; search only |

## The Blocked bar (HIGH)
Only `needs_you` (+ freshly `done`) earn an unread badge. Blocked requires an
EXPLICIT user-ownership marker (`on <user>`, `awaiting approval`, an owned
un-actioned D-number, or a paused decision the user holds). Staleness or
"still running" must NEVER promote to Blocked. blocked_external is waiting, but
not on the user — it stays calm and out of the Blocked bucket.

## Shape-per-status legend (color-blind safe; each status = shape + color)
- red up-triangle  = blocked / needs you   (most urgent)
- green diamond     = review (agent-verified)
- blue dot          = done (finished)
- (no glyph, dim)   = working / parked / archived
