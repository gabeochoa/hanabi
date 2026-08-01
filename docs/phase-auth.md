# Phase AUTH — device-code login flow (config-driven, never hardcoded)

## Goal
Let a user authenticate hanabi to a real backend WITHOUT ever pasting a token or
editing a config file: an in-app device-code flow. The app requests a device code,
shows the user a short `userCode` + a verification URL, polls until the user
approves in their browser, then receives + persists a bearer token. This replaces
the "export HANABI_TOKEN=…" step for real users. It MUST stay:
- **Config-driven**: every endpoint/field name comes from Config (env/file), NEVER
  hardcoded. No real URL, client_id, key, or company name anywhere in the repo.
- **Mock-testable**: the entire state machine must be exercised by a FAKE auth
  server implemented in the mock/test layer — zero dependency on any real service.
- **Opt-in**: when no auth endpoints are configured, the app behaves exactly as
  today (mock default; or static HANABI_TOKEN if provided). Auth UI only appears
  when the http backend is selected AND has no token AND auth endpoints ARE set.

## Standard device-code shape (RFC 8628-style, generic)
1. POST {auth_device_path}  (body: optional client_id/scope from config)
   -> { device_code, user_code, verification_uri, interval, expires_in }
2. Show the user: user_code + verification_uri (they open it, enter the code).
3. POST {auth_token_path} every `interval` sec (body: device_code, grant_type)
   -> pending: { error: "authorization_pending" } (keep polling)
   -> done:    { access_token, refresh_token?, expires_in? }
4. Persist the token (see persistence) and switch the live client to authenticated.
5. Refresh (optional, if refresh_token given): POST {auth_token_path} with the
   refresh grant when a request 401s. Best-effort; on failure, re-run the flow.

All field names (device_code, user_code, verification_uri, interval, access_token,
error, authorization_pending, etc.) MUST be Config-overridable with generic
defaults, exactly like the existing field_* mapping.

## Scope / files (own ONLY these)
- `src/api/config.cpp` + `src/api/client.h` (Config struct): add auth config fields:
    auth_device_path, auth_token_path, auth_client_id, auth_scope,
    field_device_code, field_user_code, field_verification_uri, field_interval,
    field_expires_in, field_access_token, field_refresh_token, field_auth_error,
    auth_pending_value ("authorization_pending"). Env names HANABI_AUTH_* +
    file keys. Generic defaults. Also add a `auth_ready()` helper
    (base_url && auth_device_path && auth_token_path non-empty).
- `src/api/auth.h` + `src/api/auth.cpp` (NEW): a graphics-free `DeviceCodeFlow`
    state machine: states { Idle, RequestingCode, AwaitingUser(showing code),
    Polling, Success(token), Failed(reason), Expired }. Pure logic + a small HTTP
    hook injected as a std::function so it can be unit-tested with a FAKE transport
    (no real network). Expose: begin(), poll_step(now), current_state(), user_code(),
    verification_uri(), token(). The real HTTP POSTs live behind the injected hook;
    http_client provides the real one, tests provide a fake.
- `src/api/http_client.cpp`: add a `post_json(path, body, headers)` helper (httplib
    Post; TLS-guarded like the existing Get) and wire the real transport hook for
    the flow. Do NOT hardcode any endpoint — read from cfg_.
- `src/api/token_store.h` + `.cpp` (NEW): persist/load the token to
    `~/.config/hanabi/token.json` (mode 0600). Load at startup (after config,
    before client build) so a previously-authed user is silently logged in. NEVER
    log the token. Add the path to .gitignore.
- `src/ecs/auth_system.h` (NEW): an overlay UI (immediate-mode, like
    settings_system.h) that renders the device-code panel: big user_code, the
    verification URL, a "waiting for approval…" state with a spinner/among the
    existing glyphs, and success/failure. Only shown when auth is needed. Reads the
    DeviceCodeFlow state. A "Cancel" / "Use offline (mock)" escape.
- `src/main.cpp`: drive the flow — if auth_ready() && no token, start the flow and
    show the overlay; on Success, persist + rebuild the client to the http backend.
- `tests/`: add a test that drives DeviceCodeFlow end-to-end against a FAKE
    transport (pending a few times, then success) + an expired path + a failure
    path. Register it in the makefile test target if that's the pattern.
- `afterhours_gaps.md`: append #22 ONLY if afterhours blocks something.
- `docs/config.example.json`: add the (commented/example, generic) auth keys.

## HARD constraints
- NEVER edit vendor/. Log gaps to afterhours_gaps.md (#22 next) instead.
- No real endpoint/client_id/key/URL/company name anywhere. Generic placeholders
  only (e.g. defaults empty or "example.invalid"). Grep your diff to confirm.
- The DEFAULT experience (no auth configured) is UNCHANGED — mock loads, tests pass.
- No `git add -A`; stage only the files listed above.
- Commit to branch `wt/phase-auth`. Do NOT push, do NOT merge, do NOT touch the
  main worktree at /Users/gabeochoa/projects/hanabi.
- Foreground commands on cli:aspen time out at 5s (30s hard cap). Run make/make test
  in BACKGROUND (`... >/tmp/auth_x.log 2>&1; echo DONE=$? >>/tmp/auth_x.log &`) and
  poll the log. Never foreground a build.

## Gates (all green on final commit)
- `make -j4` and `make -j4 HANABI_TLS=1` → 0 warnings, 0 errors.
- `make test` → all pass (existing 3 + your new auth test), perf gate PASS. If
  FirstFrame is 245-255ms, re-run once (box jitter, best-of-3); report both numbers.
- The new auth test must prove the FULL state machine on a FAKE transport (no real
  network): request→pending×N→success→token; plus expired + failure paths.

## Evidence to report
- Branch + worktree path (do not merge).
- Files touched + commit hash(es).
- Build results (both TLS modes) + test results + perf numbers.
- A screenshot of the auth overlay. To capture it headlessly, add a screenshot
  affordance like HANABI_VIEW: e.g. HANABI_AUTH_DEMO=1 forces the overlay into the
  AwaitingUser state with a FAKE user_code/URL (mock, no real service) so the panel
  can be photographed via `output/hanabi.exe --screenshot`. Capture dark + light.
- Confirmation that with NO auth configured, the app is byte-for-byte the same
  behavior (mock loads, all prior tests pass).
- Any afterhours gap (#22) logged + why.

## Notes
- Keep the state machine PURE and small; the UI just renders its state. The value
  is: real users authenticate in-app, and it's fully proven without a real server.
- Restore any settings.json / token.json you touch. Kill stray hanabi.exe procs.
