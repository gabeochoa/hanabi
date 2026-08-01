# Phase STREAM — live token-by-token reply (SSE), mock-first & config-driven

## Why
Phase SEND made replies functional but SYNCHRONOUS: `send_message` blocks in a
worker thread and returns ONE complete assistant Message, appended all-at-once.
The api-parity doc (docs/api-parity.md, "stream" row) specifies the real backend
delivers the reply over a user-scoped SSE channel with event kinds
text / thinking / tool-call / done / title-update. This phase makes the reply
arrive INCREMENTALLY — the assistant bubble appears immediately and fills in
token-by-token — mock-first and fully testable, with a config-driven SSE seam
for the http adapter.

## Design — a streaming seam alongside the synchronous one (do NOT remove send_message)
Add a streaming send that reports incremental chunks. Keep it PURE + testable:

- New Client capability + method (mirrors supports_send):
    `virtual bool supports_stream() const { return false; }`
    `virtual void send_message_streaming(const std::string& session_id,
        const std::string& prompt, const StreamSink& sink)`
  where `StreamSink` is a small struct of std::function callbacks:
    on_delta(const std::string& text_chunk)   // append to the in-progress reply
    on_event(StreamEvent)                       // thinking / tool-call / title / etc.
    on_done(const Message& final)               // final assembled assistant msg
    on_error(const std::string&)
  Default impl: if !supports_stream(), fall back to calling send_message() and
  invoking on_done() once (so a non-streaming adapter still works via this path).

- StreamEvent: a small typed enum + optional payload (Text, Thinking, ToolCall,
  Done, TitleUpdate, Error) so the UI can show a "thinking…" affordance and a
  live tool-call line. Keep the enum generic; no backend-specific kinds.

- MockClient: supports_stream()=true. send_message_streaming SPLITS a synthetic
  generic reply into word/token chunks and delivers them over time. Because the
  loader runs per-frame, the mock should NOT sleep on a worker thread; instead
  expose the token queue so the loader drains N tokens/frame (see Loader below).
  Simplest clean approach: the mock builds the full reply + a vector<string> of
  chunks up front and hands them to the loader via the sink incrementally as the
  loader ticks. Pick the cleanest structure — the KEY property is: tokens appear
  over multiple frames, deterministically, no real timers/network in tests.

- HttpClient: supports_stream() only when a CONFIGURABLE stream path is set
  (cfg_.stream_path, e.g. "/sessions/{id}/stream" or a chat POST that returns
  text/event-stream). Parse SSE frames ("data: {json}\n\n"), mapping event kinds
  via config field names (field_event_type, field_event_text, etc. — generic
  defaults). httplib supports a content receiver for streamed responses; use it
  behind the TLS guard. NEVER hardcode the endpoint or event names. If wiring
  real SSE parsing is too large to fully finish, implement the PARSER as a pure
  function (unit-tested with fixture SSE text) + the config seam, and note any
  transport limit honestly — but the mock streaming path MUST be complete.

## Loader + UI (the visible payoff)
- components.h: streaming state — e.g. app.streamActive, app.streamSessionId,
  app.streamBuffer (the in-progress assistant text), a token queue, a
  streamPhase (Thinking / Streaming / Done). Mirror the existing one-shot flag
  conventions + comments.
- loader_system.h: when a streamed send starts, immediately append a User bubble
  AND an empty/placeholder Assistant bubble, then each frame drain a few tokens
  from the queue into app.streamBuffer and update that Assistant message's text
  live. On done, finalize the message + refresh the cache. This is the same
  frame-tick idiom the loader already uses.
- main_pane_system.h: the in-progress assistant bubble renders app.streamBuffer;
  show a subtle "thinking…" / caret / cursor affordance while streamPhase!=Done.
  When streaming, the composer Send shows the existing "sending…"/disabled state.
  Route the transcript composer's Send through the STREAMING path when
  supports_stream(), else the existing synchronous path (no regression).

## Files you own (touch ONLY these)
- src/api/client.h (StreamSink/StreamEvent types, supports_stream,
  send_message_streaming default), src/api/config.cpp + client.h Config
  (stream_path + field_event_* with generic defaults),
  src/api/mock_client.h, src/api/http_client.h/.cpp,
  src/ecs/components.h, src/ecs/loader_system.h, src/ecs/main_pane_system.h,
  tests/ (new test_stream: mock streaming delivers chunks over ticks → final
  message assembled correctly; SSE parser fixture test if you implement it),
  makefile (wire the test), docs/config.example.json (generic stream keys),
  afterhours_gaps.md (append #22 ONLY if afterhours blocks something).

## HARD constraints
- NEVER edit vendor/. Log a gap (#22 next) instead.
- No real endpoint/URL/token/company name — code, comments, defaults, fixtures.
  Generic placeholders only (example.invalid). The streamed mock reply must be
  generic and MUST NOT contain any company name (assert it in the test).
- Mock is the zero-config default; streaming works offline against the mock with
  no config. Browse + the existing synchronous send path stay unchanged when
  streaming is not used. Do NOT remove send_message.
- No git add -A; stage only listed files. Commit to branch `wt/phase-stream`.
  Do NOT push, do NOT merge, do NOT touch the main worktree.
- Foreground commands on cli:aspen time out at 5s (30s hard cap). Run make /
  make test in BACKGROUND and poll the log. NEVER foreground a build.

## Gates (all green on final commit)
- `make -j4` and `make -j4 HANABI_TLS=1` → 0 warnings, 0 errors.
- `make test` → ALL pass (existing 5 + your new stream test), perf gate PASS
  (best-of-6; if it FAILs, re-run isolated `bash scripts/measure_launch.sh` and
  report the isolated number — the box is often under load).
- The stream test proves the mock path delivers chunks across multiple ticks and
  assembles the final assistant message, deterministically, no network/timers,
  and asserts no company name in the reply.

## Evidence to report
- Branch + worktree path (state you did NOT merge/push).
- Files touched + commit hash(es).
- Build (both TLS) + test results (name the new test) + perf number.
- Screenshot(s) proving live streaming: capture MID-STREAM (partial assistant
  bubble) AND completed. To capture headlessly, add a demo affordance like
  HANABI_STREAM_DEMO=1 that starts a mock stream and, for the mid-stream shot,
  renders after only K tokens have drained. Upload via
  `bash ~/.navi/SKILLS/file-transfer/navi-transfer.sh upload <png>` and give the
  manifold:// handles. Capture REAL rendered output.
- Confirm no-config browse + synchronous send unchanged; no leaks; 0 vendor edits.
- Restore any settings.json touched; kill stray hanabi.exe; worktree clean.
- Anything deferred + why (e.g. real SSE transport partially wired but parser
  fully tested — say so honestly).
