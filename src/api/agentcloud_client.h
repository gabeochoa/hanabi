#pragma once

// api::Client over the agentcloud orchestrator.
//
// The transport is one WebSocket speaking a keyed-fold protocol, which is a
// different shape from the REST+SSE the Config field-mapping was built for —
// so this is a sibling adapter rather than another set of config values. See
// ws_socket.h for the socket and agentcloud_auth.h for the credential.
//
// THIS SLICE IMPLEMENTS list_sessions() AND NOTHING ELSE. Everything past the
// session list needs the fold, which is its own piece of work; those overrides
// are deliberately left on Client's defaults (unsupported) rather than stubbed
// to something that looks like it works.
//
// WHY A SOCKET PER CALL. `list` is a pre-attach control command on a short-
// lived connection that the reference client closes immediately, and the
// server rejects subscription fan-out today — one attached session per socket.
// A synchronous list is therefore honest: connect, ask, read one reply, close.
// The long-lived socket arrives with attach, in the slice that needs it.

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "client.h"
#include "agentcloud_auth.h"

namespace api {

class AgentcloudClient : public Client {
   public:
    explicit AgentcloudClient(agentcloud::AuthConfig cfg);
    ~AgentcloudClient() override;

    Result<std::vector<SessionSummary>> list_sessions() override;

    Result<Session> get_session(const std::string& id) override;
    Result<Session> get_session(const std::string& id, int limit) override;

    // Sending is real; steering maps onto the same command with apply set to
    // interrupt. Streaming is how this protocol natively delivers a reply, so
    // the loader is pointed at that path rather than the blocking one.
    bool supports_send() const override { return ready(); }
    bool supports_steer() const override { return ready(); }
    bool supports_stream() const override { return ready(); }

    // rename_v1 is announced on attach, so the capability question is settled
    // per session rather than per client: this says the verb exists, and
    // rename_session re-checks the hello it actually got before sending.
    bool supports_rename() const override { return ready(); }
    Result<std::string> rename_session(const std::string& session_id,
                                       const std::string& title) override;

    void send_message_streaming(const std::string& session_id,
                                const std::string& prompt,
                                const StreamSink& sink) override;
    Result<Message> send_message(const std::string& session_id,
                                 const std::string& prompt) override;
    Result<Message> steer(const std::string& session_id,
                          const std::string& prompt) override;

    std::string backend_label() const override;

    // True when the HANABI_AC_* environment names a reachable configuration.
    // make_client checks this before choosing us over the mock.
    [[nodiscard]] bool ready() const { return auth_.config().configured(); }

   private:
    // One request/reply over a short-lived socket on the control channel.
    // Returns the decoded `msg` object as raw JSON text, or empty with *error.
    std::string round_trip(const std::string& payload_json,
                           const std::string& expect_type,
                           std::string* error);

    // attach, then input, then read the turn out as it arrives. `apply` is
    // required on the wire -- there is no server-side default.
    void run_turn(const std::string& session_id, const std::string& prompt,
                  const std::string& apply, const StreamSink& sink);

    // attach + page on ONE socket: attach binds the principal for the
    // subscription and page inherits it, so splitting them would re-attach for
    // nothing. Fills `out` and returns the hello `msg` JSON, or empty + *error.
    std::string attach_and_page(const std::string& id, int limit, Session* out,
                                std::string* error);

    agentcloud::TokenCache auth_;
};

namespace agentcloud {

// The `sessions` reply body -> SessionSummary rows, newest first.
//
// Split out of the client because it is the half worth testing and the only
// half that needs no proxy, no credential and no socket: hand it a captured
// reply and assert the mapping. Real payloads carry nulls where the schema
// suggests strings, which is exactly the kind of thing a fixture pins down.
//
// Returns empty for anything it cannot read; never throws.
std::vector<SessionSummary> parse_sessions_reply(const std::string& msg_json);

// A `page` reply's frames -> transcript messages, oldest first.
//
// This is the keyed fold, and the reason it is not a message list: the wire
// carries EVENTS, and a turn is assembled from several of them. An assistant
// paragraph is a `block{kind:text}`; a tool call is a `tool_intent` whose
// result arrives later as a separate `tool_result` naming the intent's seq.
// So tool rows are completed retroactively rather than parsed in place.
//
// Unknown event types fold as nothing and never throw -- the server's own
// contract is that the vocabulary grows, and one new variant must not take the
// transcript down with it.
std::vector<Message> parse_page_frames(const std::string& msg_json);

// The `tokens` bag on an attach greeting -> ContextUsage.
//
// Exposed for the test for the same reason parse_sessions_reply is: it is the
// half worth pinning down and it needs no proxy, no credential and no socket.
// The choice it encodes — budget as the denominator, window ignored — is the
// whole point of the meter, and only a fixture can hold it still.
ContextUsage parse_context_usage(const std::string& hello_json);


//
// Live text arrives BOTH ways, which is the trap. `block_delta{delta:"append"}`
// is a true increment and must be emitted as-is. A `value` frame (what
// attaching mid-turn hands you) and the settled `durable block` at the end
// both carry the WHOLE block, so emitting those verbatim after streaming the
// appends would print the reply twice. Kind says which, and the caller diffs
// only the accumulated ones.
struct LiveFrame {
    enum class Kind {
        Ignore,
        BlockStart,   // a new block began; the per-block buffer resets
        TextAppend,   // a TRUE append -- emit payload as-is
        Text,         // the ACCUMULATED text at this key -- diff before emitting
        Thinking,
        ToolCall,
        Title,
        Finished,
    };
    Kind kind = Kind::Ignore;
    // TextAppend: the new text only. Text/Thinking: the whole block so far.
    // ToolCall/Title: a short label.
    std::string payload;
};

// Classify one `{"type":"frame",...}` server message. Never throws; anything
// unrecognised is Ignore, because the event vocabulary grows.
//
// TWO ENTRY POINTS, and the _parsed one is what production uses. The websocket
// receive loop has already parsed each frame to read its "type", so handing
// this the string form meant dump()-ing that object back to text and parsing
// it again — parse -> dump -> parse for every frame, at token rate, measured
// at 2.3 us/frame against 0.02 us for reading the parsed object.
//
// DELIBERATELY NOT AN OVERLOAD. nlohmann::json converts implicitly from a
// string literal, so `classify_live_frame("")` against a json overload is
// ambiguous rather than obvious -- it broke four existing call sites the
// moment it was tried. Two names, no trap.
LiveFrame classify_live_frame_parsed(const nlohmann::json& root);
LiveFrame classify_live_frame(const std::string& msg_json);

// Fold a durable `session_renamed` frame onto a summary's title.
//
// The rename echo is the ONLY thing that may change a title: the client asks,
// the server settles, and this is where the settled value lands. Returns false
// (leaving `summary` untouched) for any other frame, a titleless rename, or
// text that is not a frame at all — the same never-throw contract the rest of
// the fold has.
bool fold_session_renamed(const std::string& msg_json, SessionSummary& summary);

// Accumulated live text -> the increment StreamSink wants. Exposed for the
// test: getting this wrong duplicates or drops text in a live bubble, and it
// is invisible until you watch a real reply arrive.
std::string delta_from_accumulated(const std::string& emitted,
                                   const std::string& accumulated);

}  // namespace agentcloud

}  // namespace api
