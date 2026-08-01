# Phase SEND — functional composer (kickoff + reply), config-driven & mock-first

## Why
Two send paths are currently stubbed (honest read-only comments, not fake):
1. **Kickoff** — the "New task" composer overlay (`composer_system.h`) has a
   Start button, but `Client::create_session(prompt)` is NEVER invoked; on Start
   it just closes and keeps the draft.
2. **Reply/continue** — the transcript composer (`main_pane_system.h`
   render_composer) has a disabled "Send" + a "replies aren't wired" caption,
   because the `Client` interface has NO method to continue an open thread.

This phase makes BOTH functional: real in the mock (in-memory append + a
believable assistant echo/ack), config-driven POST in the http adapter, wired
through the async loader. It turns hanabi from browse-only into interactive.

## Design (mirror the existing loader async pattern)
- Add ONE new Client method: `Result<api::Message> send_message(const std::string& session_id, const std::string& prompt)`.
  - MockClient: append a User message (from prompt) to the in-memory session's
    messages, then synthesize a short Assistant ack/echo message, append it too,
    update the summary preview + updated_at, and RETURN the assistant message
    (so the caller can append it live). Deterministic, no network.
  - HttpClient: POST to a CONFIGURABLE path (`cfg_.chat_path`, default e.g.
    "/sessions/{id}/messages") with a JSON body { prompt } (body field name
    configurable: `cfg_.field_prompt`, default "prompt"); parse the returned
    message via the SAME field mapping already used for transcripts. NEVER
    hardcode the endpoint. Reuse the `post_json` helper Phase AUTH added.
- Kickoff already has `create_session(prompt)`; WIRE it (it's currently unused):
  - MockClient::create_session already creates an in-memory session (verify);
    ensure it returns the new id and the session shows up in list + opens.
  - HttpClient::create_session: POST to `cfg_.chat_path` WITHOUT an id (or a
    configurable kickoff path `cfg_.kickoff_path`); parse the new session id.
- Loader (`loader_system.h`): add request flags + async futures for BOTH:
  - `app.requestSendPrompt` (string) + `app.requestSendSessionId` (string) →
    async send_message → on success append the returned message(s) to
    `app.openSession->messages`, refresh the cache entry, clear the draft.
  - `app.requestKickoffPrompt` (string) → async create_session → on success
    request-open the new session as a tab + refresh the list.
  - Follow the EXACT future/poll/LoadState idiom already in the file. Add a
    small sending state so the UI can show "Sending…" and re-disable Send while
    in flight (no double-send).

## Files you own (touch ONLY these)
- `src/api/client.h` — add `send_message` (pure virtual or default-fail like
  create_session so other adapters still compile), + any config fields
  (chat_path, kickoff_path, field_prompt) on Config with generic defaults.
- `src/api/config.cpp` — load the new config fields (env HANABI_CHAT_PATH etc +
  file keys), generic defaults.
- `src/api/mock_client.h` — implement send_message + verify/finish create_session.
- `src/api/http_client.h`/`.cpp` — implement send_message + create_session via
  post_json, config-driven.
- `src/ecs/components.h` — add the request/state flags (requestSendPrompt,
  requestSendSessionId, requestKickoffPrompt, a sending LoadState/bool). Mirror
  the existing one-shot flag conventions + comments.
- `src/ecs/loader_system.h` — wire both async paths.
- `src/ecs/main_pane_system.h` — ENABLE the transcript Send button: on click
  with non-empty draft, set requestSend* + clear the local draft; show "Sending…"
  while in flight; drop the "read-only" caption (replace with a normal hint).
- `src/ecs/composer_system.h` — on Start with text, set requestKickoffPrompt +
  close the overlay (real kickoff now).
- `tests/` — add a test proving send_message + create_session on the MOCK
  (append User + Assistant, preview/updated_at change, returned message correct;
  kickoff creates a listable+openable session). Wire into the makefile test
  target following the existing pattern.
- `docs/config.example.json` — add the generic chat/kickoff/prompt keys.
- `afterhours_gaps.md` — append #22 ONLY if afterhours blocks something.

## HARD constraints
- NEVER edit vendor/. Log a gap (#22 next) instead.
- No real endpoint/URL/token/company name anywhere — code, comments, defaults,
  fixtures. Generic placeholders only. Grep the full diff before finishing.
- Mock is the zero-config default and stays fully functional offline. With no
  http config, send/kickoff work against the mock. Existing behavior for browse
  is unchanged.
- No `git add -A`; stage only the listed files. Commit to branch `wt/phase-send`.
  Do NOT push, do NOT merge, do NOT touch the main worktree
  (/Users/gabeochoa/projects/hanabi).
- Foreground commands on cli:aspen time out at 5s (30s hard cap). Run make /
  make test in BACKGROUND (`( make -j4 >/tmp/send_b.log 2>&1; echo DONE=$? >>/tmp/send_b.log ) &`)
  and poll the log. NEVER foreground a build.

## Gates (all green on final commit)
- `make -j4` and `make -j4 HANABI_TLS=1` → 0 warnings, 0 errors.
- `make test` → ALL pass (existing 4 + your new send test), perf gate PASS. The
  gate is best-of-6 now; if it still FAILs, the box is under load — re-run it
  isolated (`bash scripts/measure_launch.sh`) and report the isolated number.
- The send test must prove the mock path end-to-end WITHOUT network.

## Evidence to report
- Branch + worktree path (state you did NOT merge/push).
- Files touched + commit hash(es).
- Build (both TLS) + test results (name the new test) + perf number.
- A screenshot of the transcript after a mock reply (User bubble + Assistant ack
  appended live) — upload via `bash ~/.navi/SKILLS/file-transfer/navi-transfer.sh upload <png>`
  and give the manifold:// handle. To capture headlessly you can pre-seed a draft
  + fire the send in the HANABI screenshot path, OR add a small demo affordance
  (like HANABI_VIEW) — your call, but capture REAL rendered output, not a mock.
- Confirm no-config browse behavior is unchanged + no company-name leaks + 0
  vendor edits.
- Restore any settings.json touched; kill stray hanabi.exe; worktree clean.
- Anything deferred + why (e.g. streaming token-by-token is NOT in scope — a
  single returned message is fine; SSE is a later phase).
