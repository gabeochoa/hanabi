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

    // True when the device-code flow has the minimum it needs: a base URL plus
    // both endpoint paths. When false, no auth UI ever appears and the app
    // behaves exactly as before (mock default, or a static HANABI_TOKEN).
    bool auth_ready() const {
        return !base_url.empty() && !auth_device_path.empty() &&
               !auth_token_path.empty();
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

    // Human-readable label for the active backend (for the status bar).
    virtual std::string backend_label() const = 0;
};

// Factory: builds the client selected by `cfg`. Falls back to the mock client
// if the http backend is requested but not configured.
std::unique_ptr<Client> make_client(const Config& cfg);

}  // namespace api
