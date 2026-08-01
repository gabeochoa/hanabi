# hanabi — real-backend API parity (Navi + AgentCloud)

Companion detail for **Phase API** in docs/phased-plan.md. This file documents how
hanabi's HTTP adapter maps onto two real backends, the client-side thread-state
derivation, and the successor (AgentCloud) adaptation. Everything here is GENERIC:
no real endpoints, keys, tokens, schemas, internal URLs, or company names. The mock
backend stays the zero-config default; the real API is selected only at runtime via
`HANABI_BACKEND` / `HANABI_API_BASE_URL` / `HANABI_TOKEN` and is never hardcoded.

## The seam
- `src/api/client.h` — the backend-agnostic interface (list, transcript, kickoff/
  continue, search, mutate, folders, auth, optional stream, optional cancel).
- `src/api/mock_client.h` — zero-config default; needs no network, no revalidation.
- `src/api/http_client.h` — the real adapter, behind runtime config only.
- `src/api/config.cpp` — reads env config + an optional untracked
  `~/.config/hanabi/config.json` for a field-name map. No real values committed.

## Backend 1 — the Navi API (proven by a Rust TUI client)
The adapter must map cleanly onto these capabilities with NO backend work:

| hanabi need        | Navi API shape                                                            |
|--------------------|---------------------------------------------------------------------------|
| session list       | GET sessions list, with `has_more` pagination                             |
| transcript         | GET per-session messages: roles user/assistant/system + block types,      |
|                    | including tool-call and tool-result blocks                                 |
| stream             | user-scoped SSE realtime channel; client filters by session_id;           |
|                    | event kinds: text / thinking / tool-call / done / title-update / etc.     |
| kickoff / continue | POST chat; omit the session id for a NEW session; response over SSE        |
| search             | hybrid vector + keyword search                                            |
| pin / archive /    | PATCH session (title, status:archived, isPinned)                          |
| title-edit         |                                                                           |
| folders            | session-folders CRUD + reorder                                            |
| workspaces         | workspaces endpoints                                                       |
| auth               | device-code flow: request code -> user enters userCode at the auth URL -> |
|                    | poll -> receive token (+ a session id); refresh via the refresh endpoint  |

Extras available (note, not required for MVP): export / share / fork / skills /
schedules / nodes / preferences.

**Verdict:** the adapter interface maps onto every one of these via config field-name
overrides; no server-side change is needed for the Navi API. Prove each with a LOCAL,
UNCOMMITTED, sanitized fixture per response shape (generic sample data only).

## Backend 2 — AgentCloud (the successor orchestration service)
Same session / message / stream / event concept-shape, with three adaptations:

1. **OpenAPI-spec-driven.** `openapi.json` is the capability source of truth —
   including default-model and harness enums the client should READ from the spec
   rather than hardcode. Targeting AgentCloud = generate/consume the client from
   `openapi.json`.
2. **`sessionOptions` + a session-options patch model** for per-session config
   (structured options object; patch to change per-session settings).
3. **Durable typed-event journal** for streaming (a recap/journal event wire-shape),
   consumed in place of / alongside the raw SSE event kinds.

All three sit behind the SAME adapter seam — no second bespoke client.

## The two real gaps (present on BOTH backends)

### Gap 1 — no explicit thread-STATE field
Neither backend exposes a high-signal state (blocked / needs-you / review / done).
They expose only:
- `status` (active / archived)
- an is-processing flag
- a sub-session status

hanabi **derives** its high-signal states client-side (recommended near-term). Derive
rules (client-side, from the primitives above):

- **needs-you / blocked** — the turn is waiting on the user (not processing, and the
  last block indicates the assistant handed control back / asked something).
- **review** — a turn finished and is awaiting the user's look (done processing, output
  present, not yet acknowledged).
- **done** — completed and acknowledged.
- **working** — is-processing is true (or a sub-session is processing) — shown as the
  running/dimmed row, no attention glyph.
- **archived / parked** — `status == archived` (or user-parked) — low-signal.

If a backend later adds a real `state` field, the adapter reads it when PRESENT
(optional, config-driven field name) and falls back to the derive rules when ABSENT.

### Gap 2 — no hard cancel/abort of a running turn
Steering / queueing works; a true cancel would need a small NEW backend endpoint.
hanabi keeps a `cancel` / abort method in the adapter interface (no-op on mock, gated
by config) so it is ready if a backend exposes it — but does not block on it.

## Constraints (hard)
- The real API is NEVER hardcoded; selected only via runtime env config.
- The mock backend stays the zero-config default (CI + default build never touch a
  real backend).
- A real-backend smoke test is env-gated (`HANABI_BACKEND=http` + base URL + token)
  and SKIPPED BY DEFAULT.
- No real endpoint / key / token / schema / internal URL / company name in the repo.
- No vendor edits.

## Live verification (real backend, done)
The generic adapter was exercised end-to-end against a real, running backend (config
supplied entirely at runtime via env — nothing committed). Results:
- LIST: fetched the full session list (100+ sessions) — the `{ sessions:[...],
  hasMore }` wrapper is handled; camelCase timestamps map via
  `HANABI_FIELD_UPDATED_AT=updatedAt` / `HANABI_FIELD_CREATED_AT=createdAt`.
- TRANSCRIPT: fetched a 100+ message transcript; roles (user/assistant/system/tool)
  parse correctly.

Two concrete things this live test surfaced and FIXED in the generic adapter:
1. **HTTPS transport was off by default.** TLS is opt-in behind `HANABI_ENABLE_TLS`,
   which the build didn't set — so an `https://` backend threw "scheme not supported"
   at runtime. Added an opt-in `make HANABI_TLS=1` (links OpenSSL + the platform TLS
   cert framework); the default zero-config/mock build stays dependency-free. Real
   backends are HTTPS, so a real deployment builds with `HANABI_TLS=1`.
2. **Block-array transcripts.** Some backends carry message content in a
   `blocks:[{type,content}]` array instead of a flat text field, so transcript text
   came back empty. The adapter now concatenates text-type blocks (configurable via
   `HANABI_FIELD_BLOCKS` / `_BLOCK_TYPE` / `_BLOCK_CONTENT` / `_BLOCK_TEXT_TYPE`) and
   notes the first non-text block type as a subtitle hint, falling back to the flat
   `field_text` when no blocks array is present. Both behaviors are generic — no
   backend-specific shape is baked in.

Still open (not blocking a read-only load): SSE streaming, search/mutate/folders as
adapter methods, and the device-code auth exchange (a static bearer token works for a
read; the interactive code→poll→token flow is future work). The derive-state rules
above are not yet applied in the http path (it currently leaves rows calm) — that is
the next adapter increment.
