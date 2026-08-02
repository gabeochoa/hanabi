# hanabi → navi API asks (for the PR thread)
Compiled from building the native client against the real API. Each is something
that would make hanabi (or any thin native/local-first client) better.

## Confirmed shapes (working today — documenting for the thread, no change needed)
- GET /api/v1/sessions -> { ... , array under top-level or "messages"/"data" } (list works)
- GET /api/v1/sessions/{id}/messages -> { messages:[{id,role,blocks:[{type,content|toolCall|toolResult}],createdAt}], hasMore, isStreaming }
- POST /api/chat { sessionId?, message } -> { sessionId, messageId, turnId }  (send/kickoff)
- POST /api/chat/steer { sessionId, message, steeringId? }                    (steer running agent)

## Asks (ranked)
1. **Backward/cursor pagination for /messages.** Today `?before=`/`offset` are ignored;
   "load older" is a full refetch of newest-N. A stable `before=<messageId>` cursor
   (or `offset`) returning the previous page would make long transcripts memory-light
   and enable true infinite-scroll-up. (par-msl/navi#4081 may cover; confirm.)
2. **Per-session `attentionState` / AI "waiting-on-you" summary** field on the session
   object. hanabi's "Blocked" view + notifications key off "is this waiting on me";
   today it's inferred from isProcessing/subSessionStatus. A first-class field (e.g.
   attentionState: needs_user | running | done, + a one-line "waiting on: <why>")
   would make the Blocked/Review views correct instead of heuristic.
3. **A settings/config read endpoint** (or fields on /whoami): user prefs like default
   model, verbosity, theme — so a native client can mirror the web setup instead of
   re-deriving. Small, high-leverage for "verify my setup" UX.
4. **AUTH 401 auto-refresh contract**: document/confirm the refresh-on-401 flow (hanabi
   has proactive token-refresh; the reactive 401->refresh->retry path needs the exact
   endpoint + response so a long-lived client never hard-logs-out mid-session).
5. **Message envelope stability**: confirm /api/v1/sessions/{id}/messages will keep the
   {messages:[...]} wrapper + blocks[] shape (hanabi parses these). Also: is there a
   documented list of block `type`s? (We handle text/tool_call/tool_result; we also see
   `error` and others — a canonical enum would let clients render each intentionally.)
6. **workspaceId filter** on /api/v1/sessions (?workspaceId=) + workspaceId on the
   session object, so folder/workspace grouping is server-driven not inferred.

## Notes for maintainers
- hanabi is fully config-driven (endpoints + JSON field names in local config; nothing
  product-specific compiled in) so it adapts to field renames without a rebuild.
- Biggest single win for a local-first client: #1 (cursor pagination) + #2 (attentionState).
