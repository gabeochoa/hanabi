#pragma once

// Phase AUTH — a PURE, graphics-free device-code login state machine.
//
// This implements the REAL navi-CLI device-code flow with ZERO knowledge of
// any real HOST and ZERO direct networking. The client mints its OWN device
// code (a UUIDv4); the two HTTP calls it needs are injected as a std::function
// transport hook, so the entire flow can be driven end-to-end by a FAKE
// transport in unit tests with no real network. http_client.cpp provides the
// real hook (reading endpoints from Config); tests provide a fake.
//
// Flow (no client_id/secret; the client generates the device code):
//   1. begin()      POST {auth_device_path} body {deviceCode:<UUIDv4>,
//                   clientType:<auth_client_type>} -> {userCode, authUrl}.
//                   -> AwaitingUser.
//   2. The UI shows userCode + authUrl; the user approves in a browser.
//   3. poll_step(now) GET {auth_token_path}?<field_poll_query>=<deviceCode>.
//      While the response reports {status:"pending"}, stays Polling; on
//      {status:"authorized",token:"..."} -> Success(token). Polls once per
//      auth_poll_interval (2s) and expires past auth_expires_in.
//
// Nothing here touches graphics or files. The deviceCode UUID is generated in
// begin() via std::random_device (no external dep). The state machine is
// deterministic given a clock (now, in epoch seconds) and the transport.

#include <cstdint>
#include <functional>
#include <string>

#include "client.h"

namespace api {

// The transport hook: perform an HTTP request and return the raw response
// body (or a failure). `method` is "GET" or "POST"; for GET the `query` is a
// pre-built query string (without a leading '?', may be empty) and `body` is
// ignored; for POST the `body` is the JSON request body. Injected so tests can
// fake it. Mirrors the Result<std::string> shape of HttpClient::get.
struct AuthResponse {
    bool ok = false;
    int status = 0;        // HTTP status, when known (0 if transport-level).
    std::string body;      // raw response body on success.
    std::string error;     // transport-level error message on failure.
};
using AuthTransport = std::function<AuthResponse(
    const std::string& method, const std::string& path,
    const std::string& query, const std::string& body)>;

class DeviceCodeFlow {
  public:
    enum class State {
        Idle,           // not started
        RequestingCode, // begin() in flight (transient)
        AwaitingUser,   // showing userCode + authUrl, awaiting approval
        Polling,        // AwaitingUser + a poll is in flight (transient)
        Success,        // token acquired
        Failed,         // hard failure (bad request, transport error, etc.)
        Expired,        // device code expired before the user approved
    };

    DeviceCodeFlow(Config cfg, AuthTransport transport)
        : cfg_(std::move(cfg)), transport_(std::move(transport)) {}

    // Kick off the flow: mint a UUIDv4 device code + request a user code. `now`
    // is epoch seconds (used to compute the expiry deadline + the poll
    // schedule). On success moves to AwaitingUser; on failure to Failed.
    State begin(int64_t now);

    // Advance the flow. Safe to call every frame; it only issues a network poll
    // once per auth_poll_interval seconds. `now` is epoch seconds. No-op unless
    // the state is AwaitingUser/Polling. Returns the current state.
    State poll_step(int64_t now);

    // Renew the bearer via the stored refresh token (no device-code flow).
    // Returns true + updates token() on success; false on any failure (caller
    // should then re-run begin()). Orthogonal to the flow State.
    bool refresh(int64_t now);

    State current_state() const { return state_; }
    const std::string& user_code() const { return user_code_; }
    // The URL the user opens to approve (authUrl). Named verification_uri for
    // the overlay, which is a generic device-code renderer.
    const std::string& verification_uri() const { return verification_uri_; }
    const std::string& device_code() const { return device_code_; }
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
    void set_demo_failed(const std::string& message) {
        error_ = message;
        state_ = State::Failed;
    }
    void set_demo_expired() { state_ = State::Expired; }

    // Generate a RFC-4122 version-4 UUID string (std::random_device based, no
    // external dependency). Exposed for the real-run proof/logging in main.cpp.
    static std::string make_uuid_v4();

  private:
    // Build the device-code POST request body {deviceCode,clientType}.
    std::string device_request_body() const;
    // Build the poll GET query string "<field_poll_query>=<deviceCode>".
    std::string poll_query() const;

    Config cfg_;
    AuthTransport transport_;

    State state_ = State::Idle;
    std::string device_code_;
    std::string user_code_;
    std::string verification_uri_;
    std::string token_;
    std::string refresh_token_;
    std::string error_;

    int64_t interval_ = 2;       // seconds between polls
    int64_t deadline_ = 0;       // epoch sec: past this -> Expired (0 = none)
    int64_t next_poll_at_ = 0;   // epoch sec: earliest next poll
};

}  // namespace api

