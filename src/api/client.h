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
    std::string sessions_path = "/sessions";
    std::string messages_path = "/sessions/{id}/messages";
    std::string chat_path;  // empty = http send disabled (opt-in)

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

    // --- Device-code auth (Phase AUTH) ------------------------------------
    // A generic RFC 8628-style device-code flow. NOTHING here names any real
    // service: the two endpoint paths default EMPTY (so auth is OFF unless
    // configured) and every JSON field name has a generic default that a
    // backend can override, exactly like the field_* mapping above.
    //
    //   HANABI_AUTH_DEVICE_PATH  POST -> {device_code,user_code,verification_uri,
    //                            interval,expires_in}     (empty = auth disabled)
    //   HANABI_AUTH_TOKEN_PATH   POST (poll) -> {access_token,...} or
    //                            {error:"authorization_pending"} (empty = disabled)
    //   HANABI_AUTH_CLIENT_ID    optional client_id sent in the request body
    //   HANABI_AUTH_SCOPE        optional scope sent in the request body
    std::string auth_device_path;  // empty by default: auth is opt-in
    std::string auth_token_path;   // empty by default: auth is opt-in
    std::string auth_client_id;
    std::string auth_scope;

    // Response field-name mapping for the device-code flow (all overridable).
    std::string field_device_code = "device_code";
    std::string field_user_code = "user_code";
    std::string field_verification_uri = "verification_uri";
    std::string field_interval = "interval";
    std::string field_expires_in = "expires_in";
    std::string field_access_token = "access_token";
    std::string field_refresh_token = "refresh_token";
    std::string field_auth_error = "error";
    // The sentinel error value that means "keep polling".
    std::string auth_pending_value = "authorization_pending";

    // Load from environment. Returns a Config with backend defaulted to "mock"
    // when nothing is configured.
    static Config from_env();

    // True when the http backend has the minimum it needs (a base URL).
    bool http_ready() const { return !base_url.empty(); }

    // True when the http backend is configured to send (kickoff + reply): a
    // base URL plus a chat path. When false, an http adapter honestly reports
    // it can't send and the composer stays in its disabled state.
    bool send_ready() const { return !base_url.empty() && !chat_path.empty(); }

    // True when the http backend is configured to STREAM a reply: a base URL
    // plus a stream path. When false, an http adapter reports supports_stream()
    // == false and the app uses the synchronous send_message path instead. The
    // mock streams unconditionally (no config needed).
    bool stream_ready() const {
        return !base_url.empty() && !stream_path.empty();
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

// Abstract data source.
class Client {
  public:
    virtual ~Client() = default;

    // Fetch the list of sessions, newest activity first (adapter/impl sorts).
    virtual Result<std::vector<SessionSummary>> list_sessions() = 0;

    // Fetch a full transcript for one session id.
    virtual Result<Session> get_session(const std::string& id) = 0;

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

    // Whether this client can send (kickoff + reply). The composer uses this to
    // decide between an enabled Send and the honest disabled caption. The mock
    // supports send; the http adapter supports it only when a chat path is
    // configured. Default false so a backend that hasn't wired send stays
    // honestly disabled.
    virtual bool supports_send() const { return false; }

    // Whether this client can STREAM a reply token-by-token (Phase STREAM).
    // The mock DOES (its whole reason for being is the offline demo story); the
    // http adapter does only when a stream path is configured. Default false so
    // a backend that hasn't wired streaming stays on the synchronous path.
    virtual bool supports_stream() const { return false; }

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
