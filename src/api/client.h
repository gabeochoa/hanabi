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
    std::string base_url;
    std::string token;
    std::string sessions_path = "/sessions";
    std::string messages_path = "/sessions/{id}/messages";

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

    // Load from environment. Returns a Config with backend defaulted to "mock"
    // when nothing is configured.
    static Config from_env();

    // True when the http backend has the minimum it needs (a base URL).
    bool http_ready() const { return !base_url.empty(); }
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

    // Human-readable label for the active backend (for the status bar).
    virtual std::string backend_label() const = 0;
};

// Factory: builds the client selected by `cfg`. Falls back to the mock client
// if the http backend is requested but not configured.
std::unique_ptr<Client> make_client(const Config& cfg);

}  // namespace api
