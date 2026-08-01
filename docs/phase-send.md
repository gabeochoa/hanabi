# Phase SEND — wire up sending (kickoff + reply/continue)

## Why
Two functional gaps remain, both honestly surfaced in the UI today:
1. **Kickoff**: the composer overlay's "Start" button does NOT call the existing
   `Client::create_session(prompt)` — it just closes and keeps the draft.
2. **Reply/continue**: there is NO `Client::send_message(id, prompt)` method, so the
   transcript composer is disabled-styled ("read-only preview — replies aren't
   wired yet"). This is the last big functional gap.

Close BOTH behind the same adapter seam. Mock-first, config-driven, never hardcoded.

## Scope / files (own ONLY these)
- `src/api/client.h`: add a virtual
    `virtual Result<Message> send_message(const std::string& session_id,
                                          const std::string& prompt)`
  with a default impl returning failure ("backend doesn't support replies") so
  adapters opt in. (Mirror the existing create_session default-stub pattern.)
- `src/api/mock_client.h`:
    * `create_session(prompt)`: ALREADY implemented (creates an in-memory
      session). Verify it returns the new id and the session shows up in
      list_sessions() (it appends to created_). If it doesn't already, make it.
    * `send_message(id, prompt)`: append a User message (prompt) AND a synthetic
      Assistant reply to the in-memory session's messages, return the Assistant
      Message. Keep it deterministic + offline. Update the session's updated_at
      and preview so the sidebar reflects the new activity.
- `src/api/http_client.cpp` / `.h`: implement BOTH against the generic REST seam
    using the `post_json` helper Phase AUTH added (or add one if missing, TLS-
    guarded like Get). Endpoints + field names come from Config — NEVER hardcoded:
      - kickoff: POST {chat_path} with NO session id  -> { id }  (new session)
      - reply:   POST {chat_path} WITH session id      -> the assistant message(s)
    Add config fields (HANABI_CHAT_PATH + any field-name maps needed, generic
    defaults) to Config in client.h + config.cpp. If the real backend streams the
    reply over SSE, a synchronous "post returns the created message(s)" shape is an
    acceptable adapter simplification for this phase (note it); SSE streaming stays
    the separate deferred item.
- `src/ecs/components.h`: add two one-shot request flags mirroring requestNewTask:
    `std::string requestSendPrompt;`  (reply into the OPEN session)
    `std::string requestKickoffPrompt;` (start a NEW session)
  plus small pending/among-flags if you need them for the async path.
- `src/ecs/loader_system.h`: service both flags with the SAME std::async +
    poll-future pattern already used for list/transcript:
    * requestKickoffPrompt -> async create_session -> on success, refresh the list
      AND requestOpenId = new id (open the new thread's tab).
    * requestSendPrompt -> async send_message(openSession.id, prompt) -> on success,
      APPEND the returned message(s) to app.openSession->messages, update the cache
      entry, clear the draft. Keep the UI responsive (no blocking). Show a small
      "sending…" state via transcriptState or a dedicated flag if needed.
- `src/ecs/main_pane_system.h` (render_composer): ENABLE the Send button when the
    draft is non-empty AND the backend supports replies; on click set
    app.requestSendPrompt = draft and clear the local draft. When the backend does
    NOT support replies (default mock DOES now; a minimal http that isn't
    configured for chat does not), keep the honest disabled caption. Remove/replace
    the "read-only preview" caption when sending IS wired.
- `src/ecs/composer_system.h` (Start button): on Start+hasText, set
    app.requestKickoffPrompt = app.composerDraft, clear the draft, close the overlay.
- `tests/`: add a test that drives BOTH through the MOCK client directly:
    * create_session -> new id appears in list_sessions.
    * send_message(id, "hi") -> the session gains a User("hi") + an Assistant msg;
      returned Message is the assistant reply. (Pure, no graphics, no network.)
  Wire it into the makefile test target like test_auth.

## HARD constraints
- NEVER edit vendor/. Log a gap (#22 next) to afterhours_gaps.md if blocked.
- No real endpoint/key/URL/token/company name anywhere — generic defaults only
  (empty or example.invalid). Grep the full diff before finishing.
- Default (unconfigured http, or mock) stays coherent: mock now SUPPORTS send, so
  the composer becomes functional on the mock — that's the desired outcome and the
  demo story. Existing tests must still pass.
- No `git add -A`; stage only the files above. Commit to branch `wt/phase-send`.
  Do NOT push, do NOT merge, do NOT touch the main worktree.
- Foreground commands on cli:aspen time out at 5s (30s hard cap). Run make/make
  test in BACKGROUND (`( make -j4 >/tmp/send_b.log 2>&1; echo DONE=$? >>/tmp/send_b.log ) &`)
  and poll the log. Never foreground a build/test.

## Gates (all green on final commit)
- `make -j4` and `make -j4 HANABI_TLS=1` -> 0 warnings, 0 errors.
- `make test` -> all pass (existing 4 + your new send test), perf gate PASS. If
  FirstFrame is 245-255ms, re-run once (best-of-6 gate, box jitter) and report both.
- The send test must prove kickoff + reply on the MOCK (no network).

## Evidence to report
- Branch + worktree path (state you did NOT merge/push).
- Files touched + commit hash(es).
- Builds (both TLS), tests (name the new one), perf numbers.
- Screenshots (mock, HANABI_BACKEND=mock): a transcript with the composer now
  showing an ENABLED Send (not the read-only caption) — dark + light. Upload via
  `bash ~/.navi/SKILLS/file-transfer/navi-transfer.sh upload <png>` and give
  manifold:// handles. If you can drive an actual send headlessly (set
  requestSendPrompt before the capture frames, like HANABI_AUTH_DEMO), capture the
  appended assistant reply too — bonus.
- Confirm existing tests still pass; confirm no company-name/endpoint leak; confirm
  0 vendor edits.
- Any deferred item (e.g. SSE streaming of the live reply) + why.

## Notes
- This makes the mock composer FULLY functional (type a reply -> see an assistant
  message appear), which is the demo we want. Real backend rides the same seam.
- Keep the synthetic mock assistant reply tasteful + generic (no company refs).
