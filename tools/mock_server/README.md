# Hanabi local mock server

A standalone dev tool that serves the **same REST + SSE shape** hanabi's http
adapter (`src/api/http_client.cpp`, configured via `src/api/client.h` +
`~/.config/hanabi/config.json`) expects — so the app can be exercised fully
**offline**, including **sending messages**, without the real navibot.dev
backend or a token.

- Pure **Python 3 stdlib** (`http.server` + threading). No install, no deps.
- Not the in-app `MockClient` — this is a separate **server process** the real
  http adapter talks to over the network, so it exercises the real transport,
  parsing, SSE, and send paths end to end.

## Run it

```bash
python3 tools/mock_server/server.py --port 8787
# or:
make mock-server                 # defaults to port 8787
make mock-server MOCK_PORT=9000  # custom port
```

Flags:
- `--port` (default `8787`)
- `--host` (default `127.0.0.1`)
- `--latency-ms N` — artificial per-request latency, to test the loading
  spinner / "don't beachball while fetching" behavior.

## Point hanabi at it

The server speaks **plain http** (not https), so a **NON-TLS build works** —
you do NOT need `HANABI_TLS=1`/OpenSSL. Build normally and run with:

```bash
make -j4    # normal non-TLS build -> ./output/hanabi.exe

HANABI_BACKEND=http \
HANABI_API_BASE_URL=http://127.0.0.1:8787/api/v1 \
HANABI_TOKEN=dev \
HANABI_CHAT_PATH=/chat \
HANABI_STREAM_PATH=/sessions/{id}/stream \
HANABI_EVENTS_PATH=/sessions/{id}/events \
./output/hanabi.exe
```

- `HANABI_API_BASE_URL` carries the `/api/v1` prefix; the adapter splits it into
  an origin (`http://127.0.0.1:8787`) + prefix (`/api/v1`) and prepends the
  prefix to every request path.
- `HANABI_TOKEN` can be **anything** — the mock ignores the bearer token
  (it accepts every request).
- `HANABI_CHAT_PATH=/chat` **opts in the SEND path** (kickoff + reply). Without
  it the composer stays honestly disabled (`send_ready()==false`).
- `HANABI_STREAM_PATH=/sessions/{id}/stream` **opts in token-by-token
  streaming** over SSE. Omit it to use the synchronous `/chat` send instead.
- `HANABI_EVENTS_PATH` defaults to `/sessions/{id}/events` already (matches the
  server), so live push works without setting it — listed here for clarity.

`make run` builds with TLS auto-enabled when OpenSSL is present; for this local
http server you don't need it — a plain `make -j4 && HANABI_...=... ./output/hanabi.exe`
is the simplest path.

### For context (real backend)

hanabi's real config points at `navibot.dev` over https with a device-code
token. This mock reproduces the same endpoint *shapes* so you can develop
against it offline; nothing here is baked into the app.

## Endpoints (under the `/api/v1` prefix)

| Method | Path | Purpose |
|--------|------|---------|
| `GET`  | `/sessions` | Session list. `?limit=N` returns first N + `hasMore=true`. |
| `GET`  | `/sessions/{id}/messages` | Transcript. `?limit=N` returns the **newest N** (still ascending) + `hasMore`. |
| `GET`  | `/workspaces` | Workspace (folder) list. |
| `GET`  | `/sessions/{id}/events` | **SSE**: `connected` frame on open, then activity frames. |
| `POST` | `/chat` | **Send**: `{prompt}` kicks off a new session (returns `{id}`); `{session_id,prompt}` replies (returns `{messages:[user,assistant]}`). Appends to the store + fires an SSE `message` event. |
| `POST` | `/sessions/{id}/stream` | **Streamed reply**: SSE `{type:"text",text:...}` frames token-by-token, then `{type:"done"}`. Appends the turn + fires an SSE `message` event. |

Both send paths produce a **deterministic canned assistant reply** that echoes
the prompt, so round-trips are testable.

### SSE frame shapes (what the adapter parses)

Events stream (`GET .../events`):
```
data: {"type":"connected","sessionId":"...","ts":...}          <- first frame, adapter ignores
data: {"sessionId":"...","event":{"type":"message"},"ts":...}  <- activity -> adapter refetches
```

Streamed reply (`POST .../stream`):
```
data: {"type":"text","text":"Mock "}
data: {"type":"text","text":"reply: "}
...
data: {"type":"done"}
```

## Seed data

- `s-tools` — rich thread **with `tool_call`/`tool_result` blocks**, pinned, in
  the **Stars** workspace (folders show).
- `s-long` — 30-message thread for **pagination / newest-N** testing, in the
  **Subscriptions** workspace.
- `s-short1`, `s-short2` — short threads (one `[P]` "waiting on you").
- `s-running` — an `isProcessing` / `subSessionStatus:"running"` session.
- `s-arch` — an `archived` session.
