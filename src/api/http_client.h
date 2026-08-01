#pragma once

// Generic REST adapter. This class is deliberately dumb: it knows how to do an
// authenticated GET and pull a configurable set of JSON field names out of the
// response. It has NO knowledge of any specific service — every URL, path,
// header, and field name comes from `Config` (populated from the environment
// at runtime). Nothing about a real endpoint is compiled into this repo.
//
// Networking uses the header-only cpp-httplib bundled under vendor/. If the
// backend is not fully configured, construction still succeeds but calls
// return a clean failure Result (the app then shows an error and the user can
// fall back to the mock backend).

#include <string>

#include "auth.h"
#include "client.h"

namespace api {

class HttpClient : public Client {
  public:
    explicit HttpClient(Config cfg) : cfg_(std::move(cfg)) {}

    std::string backend_label() const override { return "http"; }

    Result<std::vector<SessionSummary>> list_sessions() override;
    Result<Session> get_session(const std::string& id) override;

    // Kickoff: POST the prompt to the configured chat path with NO session id;
    // read the new session id out of the response. Requires cfg.send_ready().
    Result<std::string> create_session(const std::string& prompt) override;

    // Reply: POST the prompt to the configured chat path WITH the session id;
    // read the assistant message out of the response. Requires cfg.send_ready().
    Result<Message> send_message(const std::string& session_id,
                                 const std::string& prompt) override;

    // The http backend can send only when a chat path is configured.
    bool supports_send() const override { return cfg_.send_ready(); }

    // The http backend can STREAM only when a stream path is configured.
    bool supports_stream() const override { return cfg_.stream_ready(); }

    // Stream a reply over SSE (Phase STREAM). POSTs the prompt to the
    // configured stream path and parses the text/event-stream response into
    // StreamEvents via the config-mapped event field names, driving `sink` as
    // frames arrive. TLS-guarded like post_json. When streaming is not
    // configured this is never reached (supports_stream() gates it) — the
    // base-class fallback (send_message + on_done) covers a non-streaming http
    // backend.
    void send_message_streaming(const std::string& session_id,
                                const std::string& prompt,
                                const StreamSink& sink) override;

  private:
    // Perform an authenticated GET against base_url + path. Returns the raw
    // body on success. Implemented in http_client.cpp.
    Result<std::string> get(const std::string& path);

    // Perform an authenticated POST of a JSON `body` against base_url + path.
    // Returns the raw response body on success. TLS-guarded exactly like get().
    Result<std::string> post_json(const std::string& path,
                                  const std::string& body);

    Config cfg_;
};

// Build the real device-code transport hook for a Config. Reads the endpoint
// origin from cfg (base_url); NEVER hardcodes any endpoint. TLS-guarded like
// HttpClient::get. Tests inject a fake transport instead of this one.
AuthTransport make_http_auth_transport(const Config& cfg);

// Pure SSE-frame parser (Phase STREAM). Feeds a block of received bytes
// (which may contain zero or more complete "data: {json}\n\n" frames plus a
// trailing partial frame) and drives `sink` for every COMPLETE frame it can
// parse, using the config-mapped event field names / type values. Any bytes
// after the last frame boundary are left in `carry` for the next call, so a
// frame split across two network reads is handled correctly.
//
// This is deliberately transport-free + side-effect-free (beyond invoking the
// sink) so it is unit-tested against fixture SSE text with no network. It
// recognizes the generic event kinds from docs/api-parity.md:
//   text -> on_delta(field_event_text)
//   thinking / tool_call / title_update -> on_event(StreamEvent)
//   done -> accumulates the final Message + on_done(...) is the CALLER's job
//           (the parser reports Done via on_event so the caller can finalize).
// A frame with no recognizable type is ignored (forward-compatible).
//
// Returns true if a `done` frame was seen (the stream is complete).
bool parse_sse_chunk(const std::string& bytes, const Config& cfg,
                     const StreamSink& sink, std::string& carry,
                     std::string& assembled);

}  // namespace api
