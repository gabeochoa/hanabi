// Unit tests for the PURE device-code auth state machine (Phase AUTH).
//
// Drives api::DeviceCodeFlow end-to-end against a FAKE in-process transport —
// NO real network, NO real service, NO real endpoint. Proves the full state
// machine: request -> authorization_pending x N -> success -> token; plus the
// expired path and the failure path. The fake transport is a tiny scripted
// server implemented right here, exactly the "mock auth server in the test
// layer" the spec calls for.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/auth.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using State = api::DeviceCodeFlow::State;

// A generic config wired for the flow. All values are GENERIC placeholders —
// nothing names any real service.
static api::Config auth_cfg() {
    api::Config c;
    c.backend = "http";
    c.base_url = "http://example.invalid/api";
    c.auth_device_path = "/auth/device";
    c.auth_token_path = "/auth/token";
    // field_* + auth_pending_value use their defaults (device_code, user_code,
    // verification_uri, interval, expires_in, access_token, error,
    // "authorization_pending").
    return c;
}

// Fake device-code response body.
static std::string device_body(int interval, int expires_in) {
    return std::string("{\"device_code\":\"DEV-abc\",\"user_code\":\"WXYZ-1234\",")
         + "\"verification_uri\":\"http://example.invalid/activate\","
         + "\"interval\":" + std::to_string(interval) + ","
         + "\"expires_in\":" + std::to_string(expires_in) + "}";
}

// --- Happy path: request -> pending x3 -> success -> token ------------------
static void test_full_flow_success() {
    std::printf("test_full_flow_success\n");
    api::Config c = auth_cfg();

    // Scripted fake server: first device POST returns the code; then the token
    // POST returns authorization_pending 3 times, then the real token.
    int token_calls = 0;
    int device_calls = 0;
    api::AuthTransport fake = [&](const std::string& path,
                                  const std::string& body) {
        api::AuthResponse r;
        r.ok = true;
        if (path == c.auth_device_path) {
            ++device_calls;
            // The request body carries no client_id/scope here (unset in cfg).
            CHECK(body.find("client_id") == std::string::npos);
            r.body = device_body(/*interval=*/5, /*expires_in=*/300);
            return r;
        }
        // token path
        ++token_calls;
        // The token request must echo the device_code we were issued + a grant.
        CHECK(body.find("DEV-abc") != std::string::npos);
        CHECK(body.find("grant_type") != std::string::npos);
        if (token_calls <= 3) {
            r.status = 400;
            r.body = "{\"error\":\"authorization_pending\"}";
        } else {
            r.body = "{\"access_token\":\"tok-secret-xyz\","
                     "\"refresh_token\":\"refresh-abc\"}";
        }
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    CHECK(device_calls == 1);
    CHECK(flow.user_code() == "WXYZ-1234");
    CHECK(flow.verification_uri() == "http://example.invalid/activate");
    CHECK(flow.current_state() == State::AwaitingUser);

    // Polling before the interval elapses is a no-op (no network call).
    CHECK(flow.poll_step(now) == State::AwaitingUser);
    CHECK(token_calls == 0);
    CHECK(flow.poll_step(now + 2) == State::AwaitingUser);
    CHECK(token_calls == 0);

    // Advance past each interval: 3 pending polls keep us AwaitingUser.
    now += 5;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #1 pending
    now += 5;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #2 pending
    now += 5;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #3 pending
    CHECK(token_calls == 3);

    // 4th poll returns the token.
    now += 5;
    CHECK(flow.poll_step(now) == State::Success);
    CHECK(token_calls == 4);
    CHECK(flow.token() == "tok-secret-xyz");
    CHECK(flow.refresh_token() == "refresh-abc");

    // Further polls after success are no-ops.
    CHECK(flow.poll_step(now + 100) == State::Success);
    CHECK(token_calls == 4);
}

// --- Expired path: user never approves; deadline passes ---------------------
static void test_expired_path() {
    std::printf("test_expired_path\n");
    api::Config c = auth_cfg();
    int token_calls = 0;
    api::AuthTransport fake = [&](const std::string& path,
                                  const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        if (path == c.auth_device_path) {
            r.body = device_body(/*interval=*/5, /*expires_in=*/30);
            return r;
        }
        ++token_calls;
        r.status = 400;
        r.body = "{\"error\":\"authorization_pending\"}";
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    // A couple of pending polls…
    CHECK(flow.poll_step(now + 5) == State::AwaitingUser);
    CHECK(flow.poll_step(now + 10) == State::AwaitingUser);
    CHECK(token_calls == 2);
    // …then time passes the 30s deadline -> Expired, and no further poll fires.
    CHECK(flow.poll_step(now + 31) == State::Expired);
    CHECK(token_calls == 2);
    CHECK(flow.current_state() == State::Expired);
}

// --- Failure path: backend denies authorization -----------------------------
static void test_failure_path() {
    std::printf("test_failure_path\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string& path,
                                  const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        if (path == c.auth_device_path) {
            r.body = device_body(5, 300);
            return r;
        }
        r.status = 400;
        r.body = "{\"error\":\"access_denied\"}";
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    CHECK(flow.poll_step(now + 5) == State::Failed);
    CHECK(flow.error() == "access_denied");
}

// --- begin() failure: transport error on the device request -----------------
static void test_begin_transport_failure() {
    std::printf("test_begin_transport_failure\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string&, const std::string&) {
        api::AuthResponse r;
        r.ok = false;
        r.error = "no response";
        return r;
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::Failed);
    CHECK(!flow.error().empty());
}

// --- Not-configured guard: auth_ready() false -> begin fails cleanly --------
static void test_not_configured() {
    std::printf("test_not_configured\n");
    api::Config c;  // defaults: no auth paths -> auth_ready() false
    CHECK(!c.auth_ready());
    api::AuthTransport fake = [&](const std::string&, const std::string&) {
        return api::AuthResponse{};
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::Failed);
}

// --- client_id/scope threading through the request body ---------------------
static void test_client_id_in_body() {
    std::printf("test_client_id_in_body\n");
    api::Config c = auth_cfg();
    c.auth_client_id = "generic-client";
    c.auth_scope = "read";
    bool saw_client = false;
    api::AuthTransport fake = [&](const std::string& path,
                                  const std::string& body) {
        api::AuthResponse r;
        r.ok = true;
        if (path == c.auth_device_path) {
            if (body.find("generic-client") != std::string::npos &&
                body.find("read") != std::string::npos)
                saw_client = true;
            r.body = device_body(5, 300);
        } else {
            r.body = "{\"access_token\":\"t\"}";
        }
        return r;
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::AwaitingUser);
    CHECK(saw_client);
    CHECK(flow.poll_step(1005) == State::Success);
}

int main() {
    std::printf("=== test_auth ===\n");
    test_full_flow_success();
    test_expired_path();
    test_failure_path();
    test_begin_transport_failure();
    test_not_configured();
    test_client_id_in_body();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
