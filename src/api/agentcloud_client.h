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

}  // namespace agentcloud

}  // namespace api
