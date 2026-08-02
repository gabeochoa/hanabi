// Unit tests for the PURE device-code auth state machine (Phase AUTH).
//
// Drives api::DeviceCodeFlow end-to-end against a FAKE in-process transport —
// NO real network, NO real service, NO real HOST. Proves the REAL navi-CLI
// shapes: the client mints its OWN deviceCode (UUIDv4), POSTs {deviceCode,
// clientType} -> {userCode,authUrl}, then GET-polls ?code=<deviceCode> ->
// {status:"pending"} x N -> {status:"authorized",token}. Plus the expired
// path (client-side ceiling) and hard-failure paths. The fake transport is a
// tiny scripted server implemented right here.
#include <cstdio>
#include <string>

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
// nothing names any real HOST.
static api::Config auth_cfg() {
    api::Config c;
    c.backend = "http";
    c.base_url = "http://example.invalid/api/v1";
    // Paths default to the navi-CLI generic paths; field names default to the
    // real shape (deviceCode, clientType, userCode, authUrl, status, token,
    // pending/authorized). Use a snappy poll interval + short ceiling.
    c.auth_poll_interval = 2;
    c.auth_expires_in = 30;
    return c;
}

// True iff `s` looks like a RFC-4122 v4 UUID (8-4-4-4-12 hex, version nibble 4,
// variant nibble in {8,9,a,b}).
static bool is_uuid_v4(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char ch = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (ch != '-') return false;
        } else {
            bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                       (ch >= 'A' && ch <= 'F');
            if (!hex) return false;
        }
    }
    return s[14] == '4' &&
           (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b' ||
            s[19] == 'A' || s[19] == 'B');
}

// --- Happy path: request -> pending x3 -> authorized -> token ---------------
static void test_full_flow_success() {
    std::printf("test_full_flow_success\n");
    api::Config c = auth_cfg();

    int poll_calls = 0;
    int code_calls = 0;
    std::string minted;  // the UUID the flow put in the body / query
    api::AuthTransport fake = [&](const std::string& method,
                                  const std::string& path,
                                  const std::string& query,
                                  const std::string& body) {
        api::AuthResponse r;
        r.ok = true;
        r.status = 200;
        if (method == "POST" && path == c.auth_device_path) {
            ++code_calls;
            // The body carries a UUIDv4 deviceCode + clientType:"cli".
            CHECK(body.find("\"clientType\":\"cli\"") != std::string::npos);
            CHECK(body.find("deviceCode") != std::string::npos);
            // Extract the deviceCode value for the poll-echo check.
            auto p = body.find("\"deviceCode\":\"");
            if (p != std::string::npos) {
                p += 14;
                auto e = body.find('"', p);
                minted = body.substr(p, e - p);
            }
            CHECK(is_uuid_v4(minted));
            r.body = "{\"userCode\":\"ZKRFZQ\",\"authUrl\":\"http://example."
                     "invalid/cli/auth?code=ZKRFZQ\"}";
            return r;
        }
        // Poll path: GET with ?code=<deviceCode>.
        CHECK(method == "GET");
        CHECK(path == c.auth_token_path);
        CHECK(query == "code=" + minted);
        ++poll_calls;
        if (poll_calls <= 3) {
            r.body = "{\"status\":\"pending\"}";
        } else {
            r.body = "{\"status\":\"authorized\",\"token\":\"tok-secret-xyz\","
                     "\"userId\":\"u1\"}";
        }
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    CHECK(code_calls == 1);
    CHECK(is_uuid_v4(flow.device_code()));
    CHECK(flow.user_code() == "ZKRFZQ");
    CHECK(flow.verification_uri() ==
          "http://example.invalid/cli/auth?code=ZKRFZQ");

    // Polling before the interval elapses is a no-op (no network call).
    CHECK(flow.poll_step(now) == State::AwaitingUser);
    CHECK(poll_calls == 0);
    CHECK(flow.poll_step(now + 1) == State::AwaitingUser);
    CHECK(poll_calls == 0);

    // Advance past each 2s interval: 3 pending polls keep us AwaitingUser.
    now += 2;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #1 pending
    now += 2;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #2 pending
    now += 2;
    CHECK(flow.poll_step(now) == State::AwaitingUser);  // poll #3 pending
    CHECK(poll_calls == 3);

    // 4th poll returns the token.
    now += 2;
    CHECK(flow.poll_step(now) == State::Success);
    CHECK(poll_calls == 4);
    CHECK(flow.token() == "tok-secret-xyz");

    // Further polls after success are no-ops.
    CHECK(flow.poll_step(now + 100) == State::Success);
    CHECK(poll_calls == 4);
}

// --- Expired path: user never approves; client-side ceiling passes ----------
static void test_expired_path() {
    std::printf("test_expired_path\n");
    api::Config c = auth_cfg();  // expires_in = 30
    int poll_calls = 0;
    api::AuthTransport fake = [&](const std::string& method,
                                  const std::string& path, const std::string&,
                                  const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        r.status = 200;
        if (method == "POST" && path == c.auth_device_path) {
            r.body = "{\"userCode\":\"AAAA11\",\"authUrl\":\"http://x/y\"}";
            return r;
        }
        ++poll_calls;
        r.body = "{\"status\":\"pending\"}";
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    CHECK(flow.poll_step(now + 2) == State::AwaitingUser);
    CHECK(flow.poll_step(now + 4) == State::AwaitingUser);
    CHECK(poll_calls == 2);
    // …then time passes the 30s ceiling -> Expired, no further poll fires.
    CHECK(flow.poll_step(now + 31) == State::Expired);
    CHECK(poll_calls == 2);
    CHECK(flow.current_state() == State::Expired);
}

// --- Failure path: server reports an unexpected status ----------------------
static void test_failure_path() {
    std::printf("test_failure_path\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string& method,
                                  const std::string& path, const std::string&,
                                  const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        r.status = 200;
        if (method == "POST" && path == c.auth_device_path) {
            r.body = "{\"userCode\":\"BBBB22\",\"authUrl\":\"http://x/y\"}";
            return r;
        }
        r.body = "{\"status\":\"denied\"}";
        return r;
    };

    api::DeviceCodeFlow flow(c, fake);
    int64_t now = 1000;
    CHECK(flow.begin(now) == State::AwaitingUser);
    CHECK(flow.poll_step(now + 2) == State::Failed);
    CHECK(flow.error() == "authorization denied");
}

// --- code request returns a non-2xx (bad deviceCode) -> Failed --------------
static void test_code_http_error() {
    std::printf("test_code_http_error\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string&, const std::string&,
                                  const std::string&, const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        r.status = 400;
        r.body = "{\"error\":\"bad request\"}";
        return r;
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::Failed);
    CHECK(!flow.error().empty());
}

// --- begin() transport failure ----------------------------------------------
static void test_begin_transport_failure() {
    std::printf("test_begin_transport_failure\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string&, const std::string&,
                                  const std::string&, const std::string&) {
        api::AuthResponse r;
        r.ok = false;
        r.error = "no response";
        return r;
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::Failed);
    CHECK(!flow.error().empty());
}

// --- Not-configured guard: no base URL -> auth_ready() false ----------------
static void test_not_configured() {
    std::printf("test_not_configured\n");
    api::Config c;  // no base_url -> auth_ready() false (mock default)
    CHECK(!c.auth_ready());
    api::AuthTransport fake = [&](const std::string&, const std::string&,
                                  const std::string&, const std::string&) {
        return api::AuthResponse{};
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::Failed);
}

// --- UUIDv4 generator sanity: distinct + well-formed ------------------------
static void test_uuid_generation() {
    std::printf("test_uuid_generation\n");
    std::string a = api::DeviceCodeFlow::make_uuid_v4();
    std::string b = api::DeviceCodeFlow::make_uuid_v4();
    CHECK(is_uuid_v4(a));
    CHECK(is_uuid_v4(b));
    CHECK(a != b);
}

// --- authorized-but-missing-token is a failure ------------------------------
static void test_authorized_missing_token() {
    std::printf("test_authorized_missing_token\n");
    api::Config c = auth_cfg();
    api::AuthTransport fake = [&](const std::string& method,
                                  const std::string& path, const std::string&,
                                  const std::string&) {
        api::AuthResponse r;
        r.ok = true;
        r.status = 200;
        if (method == "POST" && path == c.auth_device_path) {
            r.body = "{\"userCode\":\"CCCC33\",\"authUrl\":\"http://x/y\"}";
            return r;
        }
        r.body = "{\"status\":\"authorized\"}";  // no token
        return r;
    };
    api::DeviceCodeFlow flow(c, fake);
    CHECK(flow.begin(1000) == State::AwaitingUser);
    CHECK(flow.poll_step(1002) == State::Failed);
}

int main() {
    std::printf("=== test_auth ===\n");
    test_full_flow_success();
    test_expired_path();
    test_failure_path();
    test_code_http_error();
    test_begin_transport_failure();
    test_not_configured();
    test_uuid_generation();
    test_authorized_missing_token();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
