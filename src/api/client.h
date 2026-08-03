#pragma once

// Backend-agnostic client interface for Hanabi.
//
// The app talks to conversations exclusively through the `Client` interface.
// Two implementations ship:
//
//   * MockClient  — deterministic in-memory sample data. This is the DEFAULT
//                   and lets the app build and run with zero configuration and
//                   no network access whatsoever.
//
//   * HttpClient  — a thin, fully generic REST adapter. It has NO knowledge of
//                   any specific service baked in: the base URL, auth token,
//                   and the small set of JSON field names it reads are ALL
//                   supplied at runtime via environment variables / a local
//                   config file (see Config below). Nothing about a real
//                   endpoint is compiled into this repository.
//
// Selection is by env var HANABI_BACKEND ("mock" default, or "http").

#include <functional>
#include <memory>
#include <string>

#include "types.h"

namespace api {

// Runtime configuration, populated from the environment. No values are
// hardcoded — an unconfigured http backend simply fails cleanly and the app
// falls back to mock data.
struct Config {
    // "mock" (default) or "http".
    std::string backend = "mock";

    // Generic REST configuration (only used by the http backend).
    // All read from the environment at startup:
    //   HANABI_API_BASE_URL  e.g. https://example.invalid/api
    //   HANABI_TOKEN         opaque bearer token (never logged, never stored)
    //   HANABI_SESSIONS_PATH path for the session list   (default "/sessions")
    //   HANABI_MESSAGES_PATH path template for a transcript, with "{id}"
    //                        (default "/sessions/{id}/messages")
    //   HANABI_CHAT_PATH     path the http adapter POSTs to for BOTH kickoff
    //                        (start a new session) and reply (continue an open
    //                        one). Empty by default, so an unconfigured http
    //                        backend honestly reports it can't send. When set,
    //                        a POST with no session id kicks off (response
    //                        carries a new {id}); a POST with a session id is a
    //                        reply (response carries the assistant message(s)).
    std::string base_url;
    std::string token;
    // Web/session URL base for the "Copy URL" action (client-side only; never
    // an API endpoint). Empty by default => a host-neutral navi://session/<id>
    // scheme is used, so we hardcode no host anywhere. Set via config
    // web_base_url / env HANABI_WEB_BASE_URL to point at a real web UI origin.
    std::string web_base_url;
    std::string sessions_path = "/sessions";
    std::string messages_path = "/sessions/{id}/messages";
    std::string chat_path;  // empty = http send disabled (opt-in)

    // --- Agent steering (Phase STEER) -------------------------------------
    // When a message is sent into a thread whose agent is CURRENTLY RUNNING,
    // it should STEER (interrupt / redirect) the running turn rather than
    // start a fresh one. Steering POSTs to a SEPARATE endpoint from chat_path.
    // FULLY generic + opt-in exactly like chat_path: empty by default so an
    // unconfigured http backend honestly reports supports_steer() == false and
    // the app just sends normally. The path uses the SAME origin-absolute
    // convention as chat_path (a leading "//" skips the base prefix — see
    // resolve_target()), so a local config sets e.g. steer_path="//api/…". The
    // request body reuses the send mapping: { field_session_id, field_prompt }.
    //   HANABI_STEER_PATH   default EMPTY (http steering disabled unless set)
    std::string steer_path;  // empty = http steering disabled (opt-in)

    // --- Memory-light transcript window -----------------------------------
    // Opening a thread fetches only the NEWEST N messages (not the whole
    // transcript) to keep the memory footprint small — you land at the bottom
    // where the newest messages are, and older ones load on demand. N is
    // configurable (HANABI_MESSAGES_WINDOW); the request appends "?limit=N" to
    // messages_path (correctly, whether or not the path already has a query).
    // The backend returns the newest N still ASCENDING (oldest-first within the
    // window) plus a "hasMore" flag when older messages exist, which the
    // adapter parses into Session::has_more_older. 0 => fetch the FULL
    // transcript (no limit) — this is how "load older" is serviced today,
    // since the backend has no working backward cursor yet.
    int messages_window = 40;
    // JSON field on the messages RESPONSE OBJECT that flags older messages
    // exist beyond the window (a wrapped response, not a bare array).
    std::string field_has_more = "hasMore";

    // --- Live events (SSE) ------------------------------------------------
    // A session's live activity stream. GET {events_path} returns
    // text/event-stream and pushes "data: {json}" frames as the session
    // changes. FULLY generic + opt-in exactly like stream_path: empty default
    // => http live-events disabled (the app just doesn't subscribe); the mock
    // is a deterministic no-op. The path templates {id} like messages_path.
    //   HANABI_EVENTS_PATH   default "/sessions/{id}/events"
    // Frame shape (verified live): the FIRST frame is a top-level
    // {type:"connected",...} (ignored — just confirms the stream opened); every
    // subsequent activity frame nests the meaningful kind under an "event"
    // object: {sessionId,event:{type:"<kind>",...},ts}. So the adapter reads
    // field_event_top_type at the top level (only "connected" matters there)
    // and otherwise field_events_obj.field_event_type for the real kind. Pure
    // telemetry kinds (field_events_ignore_type, e.g. "context_usage") are
    // ignored; anything else that implies the transcript changed triggers a
    // debounced re-fetch of the open session's newest-N. All overridable.
    std::string events_path = "/sessions/{id}/events";
    std::string field_events_obj = "event";       // the nested activity object
    std::string field_event_top_type = "type";    // top-level type ("connected")
    std::string event_type_connected = "connected";
    // Telemetry event kinds that fire constantly and must NOT trigger a
    // refetch (comma-free single value; the loader compares against it).
    std::string event_type_ignore = "context_usage";


    // JSON field-name mapping. The adapter reads these keys out of whatever
    // objects the backend returns, so the client can be pointed at different
    // shapes without code changes. Defaults are deliberately generic.
    std::string field_id = "id";
    std::string field_title = "title";
    std::string field_updated_at = "updated_at";
    std::string field_status = "status";
    std::string field_preview = "preview";
    std::string field_messages = "messages";
    std::string field_role = "role";
    std::string field_text = "text";
    std::string field_created_at = "created_at";

    // Some backends put message content in a BLOCKS array (e.g. a transcript
    // where each message has blocks:[{type:"text",content:"..."}]) rather than
    // a flat text field. When field_blocks names a present array, the adapter
    // concatenates the content of every block whose type == field_block_text_type,
    // reading the text from field_block_content. All configurable; when the
    // named blocks array is absent it falls back to the flat field_text.
    std::string field_blocks = "blocks";
    std::string field_block_type = "type";
    std::string field_block_content = "content";
    std::string field_block_text_type = "text";
    // Image-block support (inline agent artifacts): a block whose type ==
    // field_block_image_type carries an image reference in field_block_image_url.
    // Only a LOCAL path / file:// URL is rendered inline (rendering never blocks
    // on the network); a remote http(s) image would need a future download step.
    std::string field_block_image_type = "image";
    std::string field_block_image_url = "url";
    // navi's real inline-artifact block type is "show" (an agent-displayed
    // image/media/artifact carrying a url + filename). Treated the same as an
    // image block: a local/file:// url renders inline. Both "image" and "show"
    // blocks are scanned for a renderable local image.
    std::string field_block_show_type = "show";

    // Tool-call / tool-result BLOCK mapping. Real assistant messages carry an
    // interleaved SEQUENCE of blocks (text, tool_call, tool_result, ...); the
    // adapter SPLITS these into ordered api::Messages so the transcript's rich
    // tool-row renderer (which triggers on Role::Tool messages) fires on real
    // data. Verified block shapes (live):
    //   tool_call:   {type:"tool_call",   toolCall:{id,name,inputs,startedAt}}
    //   tool_result: {type:"tool_result", toolResult:{output,status,
    //                                                  toolCallId,completedAt}}
    // All field names overridable so the adapter matches a backend without
    // code changes; nothing endpoint-specific is baked in.
    std::string field_block_tool_call_type = "tool_call";
    std::string field_block_tool_result_type = "tool_result";
    std::string field_tool_call_obj = "toolCall";      // wrapper on a tool_call
    std::string field_tool_result_obj = "toolResult";  // wrapper on a tool_result
    std::string field_tool_name = "name";
    std::string field_tool_inputs = "inputs";
    std::string field_tool_started_at = "startedAt";
    std::string field_tool_id = "id";                  // toolCall.id
    std::string field_tool_output = "output";
    std::string field_tool_status = "status";
    std::string field_tool_result_call_id = "toolCallId";
    std::string field_tool_completed_at = "completedAt";
    // Keys the inputs blob commonly carries, surfaced onto the tool row's
    // command line (best-effort; a totally opaque blob is shown truncated).
    std::string field_tool_input_command = "command";
    std::string field_tool_input_node = "node";

    // Chat/send field mapping (used by the http adapter's send_message). All
    // generic + overridable, exactly like the field_* mapping above.
    //   - The request body sends the prompt under field_prompt and, for a
    //     reply, the target session id under field_session_id.
    //   - The kickoff response is read for a new session id under field_id.
    //   - The reply response yields the assistant message(s): either a single
    //     message object, or an array of them under field_messages. Each is
    //     read with the same field_role / field_text / field_created_at /
    //     field_id / block mapping the transcript reader uses.
    std::string field_prompt = "prompt";
    std::string field_session_id = "session_id";

    // --- Streaming (Phase STREAM) -----------------------------------------
    // Live token-by-token replies over a server-sent-events (SSE) channel. Like
    // every other path here this is FULLY generic + opt-in: the stream path
    // defaults EMPTY (so an http backend honestly reports supports_stream()
    // == false unless configured), and each SSE event field name has a generic
    // default a backend can override, exactly like the field_* mapping above.
    //
    //   HANABI_STREAM_PATH   path the http adapter POSTs to for a STREAMED
    //                        reply; the response is text/event-stream ("data:
    //                        {json}\n\n" frames). Empty by default => http
    //                        streaming disabled (the app uses the synchronous
    //                        send_message path instead). The mock streams with
    //                        NO config at all.
    //
    // Each SSE data frame is a JSON object read with these field names:
    //   field_event_type   the event kind ("text"/"thinking"/"tool_call"/
    //                       "done"/"title_update"); generic values, all
    //                       overridable so the adapter matches a backend's
    //                       naming without code changes.
    //   field_event_text   the text delta carried by a text/thinking event.
    //   field_event_title  the new title carried by a title_update event.
    std::string stream_path;  // empty = http streaming disabled (opt-in)
    std::string field_event_type = "type";
    std::string field_event_text = "text";
    std::string field_event_title = "title";
    // The generic event-type VALUES the adapter recognizes (all overridable).
    std::string event_type_text = "text";
    std::string event_type_thinking = "thinking";
    std::string event_type_tool_call = "tool_call";
    std::string event_type_done = "done";
    std::string event_type_title_update = "title_update";

    // --- User settings / config (feature #4) ------------------------------
    // Read user/account settings from the backend so the app can verify it is
    // set up correctly. FULLY generic + config-driven exactly like every other
    // path: empty default => http settings-read disabled (get_settings() reports
    // it's unsupported). On the real backend the reachable endpoint is GET /whoami,
    // which returns {userId, bankId, counts:{sessions, assets, schedules,
    // authoredSkills}}; the adapter maps those onto UserSettings via the
    // field_settings_* mapping below. A backend with a different shape just
    // overrides the field names — nothing about any endpoint is compiled in.
    //   HANABI_SETTINGS_PATH  default "/whoami" (probed live to be the real
    //                         settings/identity endpoint on the real backend)
    std::string settings_path = "/whoami";
    // WRITE side (settings sync). EMPTY by default => settings-write disabled
    // (update_settings() reports unsupported). This deliberately carries NO
    // real endpoint value — the zero-config default is the in-memory mock. A
    // real backend only activates the write when the user sets this in their
    // LOCAL config (never committed). Mirrors the read settings_path pattern.
    std::string settings_update_path;  // empty = http settings-write disabled
    std::string field_settings_user_id = "userId";
    std::string field_settings_bank_id = "bankId";
    // The nested object carrying the counts, and its member field names.
    std::string field_settings_counts = "counts";
    std::string field_settings_sessions = "sessions";
    std::string field_settings_assets = "assets";
    std::string field_settings_schedules = "schedules";
    std::string field_settings_skills = "authoredSkills";

    // --- Device-code auth (Phase AUTH) ------------------------------------
    // The REAL navi-CLI device-code flow. NOTHING here names any real HOST:
    // the base URL is user-supplied (base_url / auth_base_url); only the two
    // generic navi-CLI PATHS are baked as defaults, and every JSON field name
    // is overridable, exactly like the field_* mapping above.
    //
    // The flow (client mints its OWN device code — no client_id/secret):
    //   1. POST {auth_device_path}  body {deviceCode:<UUIDv4>,clientType:"cli"}
    //      -> {userCode:"<short>", authUrl:"<url to open>"}. (empty path = off)
    //   2. User opens authUrl in a browser and approves.
    //   3. GET  {auth_token_path}?<field_poll_query>=<deviceCode>  every 2s
    //      -> {status:"pending"} | {status:"authorized",token:"<bearer>"}.
    //
    //   HANABI_AUTH_DEVICE_PATH  code-request path  (default /api/cli/auth/code)
    //   HANABI_AUTH_TOKEN_PATH   poll path          (default /api/cli/auth/poll)
    //   HANABI_AUTH_BASE_URL     override the auth ORIGIN. The auth paths are
    //                            SIBLINGS of the API (e.g. /api/cli/auth/* is
    //                            NOT under base_url's /api/v1 prefix), so by
    //                            default the auth transport uses base_url's
    //                            scheme+host ORIGIN (dropping any path prefix).
    //                            Set this to point auth at a different origin.
    std::string auth_device_path = "/api/cli/auth/code";  // navi-CLI default
    std::string auth_token_path = "/api/cli/auth/poll";   // navi-CLI default
    // Refresh path (POST {refreshToken:<tok>} -> {token:<new bearer>,
    // refreshToken?:<rotated>}). The token is a ~30-day TTL; refresh() lets the
    // app renew it without re-running the whole device-code flow. Empty = off.
    //   HANABI_AUTH_REFRESH_PATH  (default /api/cli/auth/refresh)
    std::string auth_refresh_path = "/api/cli/auth/refresh";
    std::string auth_base_url;     // empty => derive origin from base_url
    std::string auth_client_type = "cli";  // sent as clientType in the body

    // Poll interval in seconds (the real backend wants 2s). Overridable but
    // NOT read from the code response (which carries no interval).
    int64_t auth_poll_interval = 2;
    // How long (seconds) to keep polling before giving up -> Expired. The code
    // response carries no expiry, so this is a client-side ceiling. 0 = never.
    int64_t auth_expires_in = 600;

    // Request field-name mapping (device-code POST body).
    std::string field_device_code = "deviceCode";
    std::string field_client_type = "clientType";
    // Poll query-param name carrying the deviceCode (GET ?code=<deviceCode>).
    std::string field_poll_query = "code";

    // Response field-name mapping for the device-code flow (all overridable).
    std::string field_user_code = "userCode";
    std::string field_auth_url = "authUrl";
    std::string field_auth_status = "status";
    std::string field_token = "token";
    // Refresh-token field: read from the authorize/refresh response AND sent as
    // the body key on a refresh POST. (Both use the same key by convention.)
    std::string field_refresh_token = "refreshToken";
    // The status VALUES the poll response reports (all overridable).
    std::string auth_status_pending = "pending";
    std::string auth_status_authorized = "authorized";

    // Load from environment. Returns a Config with backend defaulted to "mock"
    // when nothing is configured.
    static Config from_env();

    // True when the http backend has the minimum it needs (a base URL).
    bool http_ready() const { return !base_url.empty(); }

    // True when the http backend is configured to send (kickoff + reply): a
    // base URL plus a chat path. When false, an http adapter honestly reports
    // it can't send and the composer stays in its disabled state.
    bool send_ready() const { return !base_url.empty() && !chat_path.empty(); }

    // True when the http backend is configured to STEER a running agent: a
    // base URL plus a steer path. When false, an http adapter reports
    // supports_steer() == false and the loader always sends normally (never
    // routes to steer). Opt-in exactly like send_ready()/stream_ready().
    bool steer_ready() const {
        return !base_url.empty() && !steer_path.empty();
    }

    // True when the http backend is configured to STREAM a reply: a base URL
    // plus a stream path. When false, an http adapter reports supports_stream()
    // == false and the app uses the synchronous send_message path instead. The
    // mock streams unconditionally (no config needed).
    bool stream_ready() const {
        return !base_url.empty() && !stream_path.empty();
    }

    // True when the http backend is configured to SUBSCRIBE to a session's
    // live events over SSE: a base URL plus an events path. When false an http
    // adapter reports supports_events() == false and the app simply never
    // opens a live subscription (transcripts still refresh on open/switch, just
    // not push-live). The mock reports false (its subscribe is a no-op) so the
    // offline demo never touches the network.
    bool events_ready() const {
        return !base_url.empty() && !events_path.empty();
    }

    // True when the http backend is configured to READ user settings: a base
    // URL plus a settings path. When false an http adapter reports
    // supports_settings() == false and get_settings() returns a clean failure
    // (the app just doesn't show backend settings). The mock returns a canned
    // UserSettings unconditionally (zero-config offline default).
    bool settings_ready() const {
        return !base_url.empty() && !settings_path.empty();
    }
    // WRITE side: only ready when a base URL AND a settings_update_path are
    // set. Empty settings_update_path (the default) keeps http writes OFF —
    // local-only persistence still works; nothing is pushed. Mirrors
    // settings_ready() for the read path.
    bool settings_write_ready() const {
        return !base_url.empty() && !settings_update_path.empty();
    }

    // True when the device-code flow has the minimum it needs: a base URL plus
    // both endpoint paths. When false, no auth UI ever appears and the app
    // behaves exactly as before (mock default, or a static HANABI_TOKEN).
    bool auth_ready() const {
        return !base_url.empty() && !auth_device_path.empty() &&
               !auth_token_path.empty();
    }
};

// --- Streaming events (Phase STREAM) --------------------------------------
//
// A live reply arrives as an ordered sequence of typed events. The enum is
// deliberately GENERIC — it mirrors the "stream" row of docs/api-parity.md
// (text / thinking / tool-call / done / title-update) without naming any
// backend-specific kind, so the same UI drives the mock and any real adapter.
enum class StreamEventKind {
    Text,         // an incremental chunk of assistant reply text (payload=text)
    Thinking,     // the agent is reasoning; drives a "thinking…" affordance
    ToolCall,     // a tool/step is running (payload = a short human label)
    Done,         // the reply is complete; on_done() carries the final Message
    TitleUpdate,  // the session title changed (payload = the new title)
    Error,        // the stream failed (payload = a human-readable reason)
};

// One streaming event: a kind plus an optional string payload whose meaning
// depends on the kind (see the enum). Kept tiny + copyable.
struct StreamEvent {
    StreamEventKind kind = StreamEventKind::Text;
    std::string payload;  // text delta / label / title / error, per kind.
};

// A small bundle of callbacks the caller (the loader) installs to receive a
// streamed reply. Every callback is optional (a default-constructed
// std::function is simply not invoked) so a caller can subscribe to only what
// it needs. The adapter drives these as the reply arrives:
//   on_delta  — append this text chunk to the in-progress assistant bubble.
//   on_event  — a non-text event (Thinking / ToolCall / TitleUpdate / …).
//   on_done   — the reply is complete; carries the final assembled Message.
//   on_error  — the stream failed; carries a human-readable reason.
struct StreamSink {
    std::function<void(const std::string& delta)> on_delta;
    std::function<void(const StreamEvent& ev)> on_event;
    std::function<void(const Message& final)> on_done;
    std::function<void(const std::string& error)> on_error;

    void emit_delta(const std::string& d) const {
        if (on_delta) on_delta(d);
    }
    void emit_event(const StreamEvent& e) const {
        if (on_event) on_event(e);
    }
    void emit_done(const Message& m) const {
        if (on_done) on_done(m);
    }
    void emit_error(const std::string& e) const {
        if (on_error) on_error(e);
    }
};

// --- Live session events (SSE) --------------------------------------------
//
// A subscription to a session's live activity stream. The caller installs a
// tiny callback that fires (on a WORKER THREAD — never the UI thread) each
// time the backend reports the session changed; the callback must be cheap and
// thread-safe (the loader just flips an atomic "refetch me" flag it polls on
// the UI thread — it never touches the ECS from the callback). The returned
// EventSubscription is an RAII handle: destroying it (or calling stop()) tears
// the worker + socket down. This mirrors the async-fetch pattern — nothing
// blocks the UI thread.
struct EventSink {
    // Called for a meaningful activity event (a new/updated message, a turn, a
    // tool call/result, session_start, or any unrecognized non-telemetry
    // kind). The payload is the raw event-kind string (for logging/debug); the
    // caller does NOT diff it — it just marks "something changed, refetch".
    std::function<void(const std::string& event_kind)> on_activity;
    // Called if the stream fails/closes so the caller can decide to reconnect.
    std::function<void(const std::string& error)> on_error;

    void emit_activity(const std::string& k) const {
        if (on_activity) on_activity(k);
    }
    void emit_error(const std::string& e) const {
        if (on_error) on_error(e);
    }
};

// RAII handle to a live subscription. Abstract so the mock returns a trivial
// no-op handle and the http adapter returns one that owns the worker thread +
// a stop flag. Destroying it stops the subscription (joins the worker).
class EventSubscription {
  public:
    virtual ~EventSubscription() = default;
    // Signal the worker to stop and (for the http impl) join it. Idempotent.
    virtual void stop() = 0;
};

// Abstract data source.
class Client {
  public:
    virtual ~Client() = default;

    // Fetch the list of sessions, newest activity first (adapter/impl sorts).
    virtual Result<std::vector<SessionSummary>> list_sessions() = 0;

    // Fetch a full transcript for one session id.
    virtual Result<Session> get_session(const std::string& id) = 0;

    // MEMORY-LIGHT fetch: only the NEWEST `limit` messages of a session (still
    // ordered oldest-first WITHIN the window). `limit <= 0` means "no limit" =
    // the full transcript, identical to get_session(id). The returned Session
    // sets has_more_older = true when older messages exist beyond the window,
    // so the UI can offer "load older". Default impl ignores the limit and
    // delegates to the full get_session(id), so a backend that hasn't wired
    // windowing still works (it just always loads everything). The http
    // adapter appends "?limit=N"; the mock returns the last N of its messages.
    virtual Result<Session> get_session(const std::string& id, int limit) {
        (void)limit;
        return get_session(id);
    }

    // Kick off a NEW session from a prompt (composer "New task"). Returns the
    // new session id on success. The mock creates an in-memory session; the
    // http adapter POSTs to a configurable path. Default impl reports that the
    // backend doesn't support kickoff, so adapters can opt in incrementally.
    virtual Result<std::string> create_session(const std::string& prompt) {
        (void)prompt;
        return Result<std::string>::failure(
            "this backend does not support creating sessions");
    }

    // Continue an OPEN session: send a user prompt into `session_id` and return
    // the assistant reply. Default impl reports the backend doesn't support
    // replies, so adapters opt in incrementally (mirrors create_session). The
    // mock appends a User message + a synthetic Assistant reply and returns the
    // assistant Message; the http adapter POSTs to the configured chat path.
    virtual Result<Message> send_message(const std::string& session_id,
                                         const std::string& prompt) {
        (void)session_id;
        (void)prompt;
        return Result<Message>::failure(
            "this backend does not support replies");
    }

    // STEER a CURRENTLY-RUNNING agent: send a user prompt into `session_id` to
    // interrupt / redirect the in-flight turn (as opposed to send_message,
    // which starts a fresh turn). POSTs to a SEPARATE endpoint from the chat
    // path. Default impl reports the backend doesn't support steering, so
    // adapters opt in incrementally (mirrors send_message). The mock appends a
    // small "(steering) <msg>" acknowledgement so the offline demo still works;
    // the http adapter POSTs to the configured steer path when set.
    virtual Result<Message> steer(const std::string& session_id,
                                  const std::string& prompt) {
        (void)session_id;
        (void)prompt;
        return Result<Message>::failure(
            "this backend does not support steering");
    }

    // Whether this client can send (kickoff + reply). The composer uses this to
    // decide between an enabled Send and the honest disabled caption. The mock
    // supports send; the http adapter supports it only when a chat path is
    // configured. Default false so a backend that hasn't wired send stays
    // honestly disabled.
    virtual bool supports_send() const { return false; }

    // Whether this client can STEER a currently-running agent (Phase STEER).
    // The http adapter does only when a steer path is configured; the mock
    // supports it unconditionally (offline demo). Default false so a backend
    // that hasn't wired steering never routes there — the loader falls back to
    // a normal send. The loader gates the steer-vs-send decision on this.
    virtual bool supports_steer() const { return false; }

    // Whether this client can STREAM a reply token-by-token (Phase STREAM).
    // The mock DOES (its whole reason for being is the offline demo story); the
    // http adapter does only when a stream path is configured. Default false so
    // a backend that hasn't wired streaming stays on the synchronous path.
    virtual bool supports_stream() const { return false; }

    // Whether this client can SUBSCRIBE to a session's live events over SSE.
    // The http adapter does only when an events path is configured; the mock
    // reports false (its subscribe is a no-op that keeps the offline demo
    // deterministic + network-free). Default false so a backend that hasn't
    // wired live events simply never subscribes.
    virtual bool supports_events() const { return false; }

    // Whether this client can READ user settings/config from the backend
    // (feature #4). The http adapter does only when a settings path is
    // configured; the mock returns true (it serves a canned object offline).
    // Default false so a backend that hasn't wired settings stays honest.
    virtual bool supports_settings() const { return false; }

    // Fetch user/account settings so the app can verify it is set up correctly.
    // The http adapter GETs the configured settings_path (e.g. /whoami) and
    // maps the response onto UserSettings; the mock returns a deterministic
    // canned object. Default impl reports the backend doesn't support it, so
    // adapters opt in incrementally (mirrors create_session/send_message).
    virtual Result<UserSettings> get_settings() {
        return Result<UserSettings>::failure(
            "this backend does not support reading settings");
    }

    // Whether this client can WRITE user settings back to the backend so the
    // web app matches local. The http adapter does only when a
    // settings_update_path is configured (empty by default => off); the mock
    // returns true (it stores in memory offline). Default false so a backend
    // that hasn't wired the write path stays honest and local-only still works.
    virtual bool supports_settings_write() const { return false; }

    // Push local user settings to the backend (best-effort, called off the UI
    // thread by the loader when a preference changes). Returns true on a
    // successful push. The http adapter PUTs the configured settings_update_path
    // ONLY when settings_write_ready(); the mock accepts + stores in memory.
    // Default impl is a no-op returning false so local-only persistence still
    // works with zero config and no error is surfaced.
    virtual bool update_settings(const UserSettings& s) {
        (void)s;
        return false;
    }

    // Open a live subscription to `session_id`'s events. The sink's callbacks
    // fire on a WORKER THREAD as activity arrives (the caller must keep them
    // cheap + thread-safe — the loader just flips a poll flag). Returns an RAII
    // handle whose destruction (or stop()) tears the subscription down; NEVER
    // returns null. Default impl: a no-op subscription (nothing ever fires) so
    // a backend without live events is safe to call. The http adapter runs the
    // real text/event-stream read on a std::thread and coalesces frames into
    // on_activity; the mock returns the same no-op handle.
    virtual std::unique_ptr<EventSubscription> subscribe_events(
        const std::string& session_id, EventSink sink) {
        (void)session_id;
        (void)sink;
        // A trivial no-op handle: nothing to stop, nothing ever fires.
        struct NoopSub : EventSubscription {
            void stop() override {}
        };
        return std::make_unique<NoopSub>();
    }

    // Continue an OPEN session, delivering the assistant reply INCREMENTALLY
    // through `sink`. Mirrors send_message but reports chunks as they arrive
    // (on_delta), non-text events (on_event), and a final assembled Message
    // (on_done) — or a failure (on_error).
    //
    // DEFAULT IMPL — a graceful fallback so a NON-streaming adapter still works
    // through this path: run the synchronous send_message() and, on success,
    // hand the whole reply to on_done() as a single (implicit) delta + done; on
    // failure call on_error(). This means the loader can always call
    // send_message_streaming() and get correct behavior regardless of whether
    // the backend truly streams — supports_stream() only decides whether the
    // reply arrives in pieces or all at once.
    virtual void send_message_streaming(const std::string& session_id,
                                        const std::string& prompt,
                                        const StreamSink& sink) {
        Result<Message> r = send_message(session_id, prompt);
        if (!r.ok) {
            sink.emit_error(r.error);
            return;
        }
        // Non-streaming backend: deliver the full text as one delta, then done.
        sink.emit_delta(r.value.text);
        sink.emit_done(r.value);
    }

    // Human-readable label for the active backend (for the status bar).
    virtual std::string backend_label() const = 0;
};

// Factory: builds the client selected by `cfg`. Falls back to the mock client
// if the http backend is requested but not configured.
std::unique_ptr<Client> make_client(const Config& cfg);

}  // namespace api
