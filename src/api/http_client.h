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

#include <memory>
#include <string>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"
#include "auth.h"
#include "client.h"

namespace api {

class HttpClient : public Client {
  public:
    explicit HttpClient(Config cfg) : cfg_(std::move(cfg)) {}

    std::string backend_label() const override { return "http"; }

    Result<std::vector<SessionSummary>> list_sessions() override;
    Result<Session> get_session(const std::string& id) override;

    // MEMORY-LIGHT windowed fetch: GET messages_path with "?limit=N" appended
    // (correctly, whether or not the path already carries a query), returning
    // only the newest N messages + Session::has_more_older parsed from the
    // response's "hasMore" flag. limit <= 0 => the full transcript (no query),
    // identical to get_session(id). This is how the loader opens a thread at
    // the bottom cheaply, and how "load older" re-fetches the whole transcript.
    Result<Session> get_session(const std::string& id, int limit) override;

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

    // The http backend can SUBSCRIBE to live events only when an events path
    // is configured.
    bool supports_events() const override { return cfg_.events_ready(); }

    // Subscribe to a session's live event stream over SSE (on a worker thread).
    // POSTs/GETs the configured events path (with {id} substituted) and feeds
    // the text/event-stream response through parse_events_frame, invoking the
    // sink's on_activity for every meaningful (non-telemetry) event. Returns an
    // RAII handle that owns the worker thread + a stop flag; destroying it (or
    // calling stop()) joins the worker. TLS-guarded like send_message_streaming
    // — an https origin without a TLS build reports on_error and stops. Caps
    // reconnects so a persistently-failing stream doesn't spin forever.
    std::unique_ptr<EventSubscription> subscribe_events(
        const std::string& session_id, EventSink sink) override;

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

// Pure LIVE-EVENT frame parser (SSE). Shares parse_sse_chunk's frame-splitting
// discipline (blank-line-delimited "data:" frames, CRLF-safe, carry across
// reads) but reads the LIVE-EVENT vocabulary instead of the reply-stream one:
//   * a top-level {type:"connected",...} frame is IGNORED (stream opened);
//   * every other frame's meaningful kind is event.type (nested under
//     field_events_obj); the pure-telemetry kind event_type_ignore
//     ("context_usage") is IGNORED (fires constantly);
//   * anything else that implies the transcript changed calls sink.on_activity
//     with the kind string (the caller coalesces/debounces + re-fetches).
// Transport-free + side-effect-free (beyond the sink) so it is unit-tested
// against fixture text. Bytes after the last frame boundary stay in `carry`.
void parse_events_frame(const std::string& bytes, const Config& cfg,
                        const EventSink& sink, std::string& carry);

// Pure BLOCK SPLITTER (tool calls). Given one raw message JSON object and the
// config field mapping, produce the ordered sequence of api::Messages it maps
// to: runs of text blocks collapse into a Role::Assistant message; each
// tool_call (+ its matching tool_result by toolCallId) becomes a Role::Tool
// message with subtitle=name, text=command (best-effort from inputs),
// tool_result=output, tool_status=status, tool_duration_ms=completedAt-
// startedAt. A message with no blocks (or only text) yields a single message
// exactly as parse_message did. ORDER is preserved so the transcript reads
// correctly. Pure + unit-tested against a fixture; the mock path never calls
// it (its Tool messages are authored directly).
std::vector<Message> split_message_blocks(const nlohmann::json& e,
                                           const Config& cfg);

}  // namespace api
