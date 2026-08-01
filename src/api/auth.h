#pragma once

// Phase AUTH — a PURE, graphics-free device-code login state machine.
//
// This implements a generic RFC 8628-style device-code flow with ZERO
// knowledge of any real service and ZERO direct networking. The single HTTP
// POST it needs is injected as a std::function transport hook, so the entire
// flow can be driven end-to-end by a FAKE transport in unit tests with no real
// network. http_client.cpp provides the real hook (reading endpoints from
// Config); tests provide a fake.
//
// Flow:
//   1. begin()      POST {auth_device_path} -> {device_code,user_code,
//                   verification_uri,interval,expires_in}. -> AwaitingUser.
//   2. The UI shows user_code + verification_uri; the user approves in a
//      browser.
//   3. poll_step(now) POST {auth_token_path} with the device_code. While the
//      response reports the pending sentinel, stays Polling; on {access_token}
//      -> Success(token). Respects `interval` (only polls once per interval)
//      and `expires_in` (-> Expired past the deadline).
//
// Nothing here touches graphics, files, or the network. The state machine is
// deterministic given a clock (now, in epoch seconds) and the transport.

#include <cstdint>
#include <functional>
#include <string>

#include "client.h"

namespace api {

// The transport hook: given a path + a JSON request body, perform an HTTP POST
// and return the raw response body (or a failure). Injected so tests can fake
// it. Mirrors the Result<std::string> shape of HttpClient::get.
struct AuthResponse {
    bool ok = false;
    int status = 0;        // HTTP status, when known (0 if transport-level).
    std::string body;      // raw response body on success.
    std::string error;     // transport-level error message on failure.
};
using AuthTransport =
    std::function<AuthResponse(const std::string& path, const std::string& body)>;

class DeviceCodeFlow {
  public:
    enum class State {
        Idle,           // not started
        RequestingCode, // begin() in flight (transient)
        AwaitingUser,   // showing user_code + verification_uri, awaiting approval
        Polling,        // AwaitingUser + a poll is in flight (transient)
        Success,        // token acquired
        Failed,         // hard failure (bad request, transport error, etc.)
        Expired,        // device code expired before the user approved
    };

    DeviceCodeFlow(Config cfg, AuthTransport transport)
        : cfg_(std::move(cfg)), transport_(std::move(transport)) {}

    // Kick off the flow: request a device code. `now` is epoch seconds (used to
    // compute the expiry deadline + the poll schedule). On success moves to
    // AwaitingUser; on failure to Failed. Returns the new state.
    State begin(int64_t now);

    // Advance the flow. Safe to call every frame; it only issues a network poll
    // once per `interval` seconds. `now` is epoch seconds. No-op unless the
    // state is AwaitingUser/Polling. Returns the current state.
    State poll_step(int64_t now);

    State current_state() const { return state_; }
    const std::string& user_code() const { return user_code_; }
    const std::string& verification_uri() const { return verification_uri_; }
    const std::string& token() const { return token_; }
    const std::string& refresh_token() const { return refresh_token_; }
    const std::string& error() const { return error_; }

    // Force-set a display state for the HANABI_AUTH_DEMO screenshot affordance
    // (pure, no network): shows a FAKE user_code + verification_uri in the
    // AwaitingUser panel so the overlay can be photographed headlessly.
    void set_demo_awaiting(const std::string& fake_code,
                           const std::string& fake_uri) {
        user_code_ = fake_code;
        verification_uri_ = fake_uri;
        state_ = State::AwaitingUser;
    }

  private:
    // Build the JSON request bodies (client_id/scope from cfg when set).
    std::string device_request_body() const;
    std::string token_request_body() const;

    Config cfg_;
    AuthTransport transport_;

    State state_ = State::Idle;
    std::string device_code_;
    std::string user_code_;
    std::string verification_uri_;
    std::string token_;
    std::string refresh_token_;
    std::string error_;

    int64_t interval_ = 5;       // seconds between polls (from response)
    int64_t deadline_ = 0;       // epoch sec: past this -> Expired (0 = none)
    int64_t next_poll_at_ = 0;   // epoch sec: earliest next poll
};

}  // namespace api
