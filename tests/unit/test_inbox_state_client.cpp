#include <atomic>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

#include <httplib.h>

#include "../../src/api/agentcloud_auth.h"
#include "../../src/api/agentcloud_client.h"
#include "../../src/api/agentcloud_hosts.h"
#include "../../src/api/inbox_state_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

static api::agentcloud::Token tok(const char* v) {
    api::agentcloud::Token t;
    t.value = v;
    t.expires_at = static_cast<int64_t>(std::time(nullptr)) + 3600;
    return t;
}

int main() {
    std::printf("=== test_inbox_state_client ===\n");
    namespace is = api::inbox_state;

    {
        api::agentcloud::AuthConfig c;
        c.mint_host = "m";
        c.verifier = "orch-verifier";
        c.host = api::agentcloud_hosts::kStandardOrchestratorHost;
        api::agentcloud::resolve_web_origin(c, "");
        CHECK(c.web_configured() && c.web_origin == api::agentcloud_hosts::kWebOrigin &&
              c.web_verifier == is::kCatVerifier);
        CHECK(c.web_auth().host == c.web_origin && c.web_auth().verifier == is::kCatVerifier &&
              c.web_auth().mint_host == "m");
        api::agentcloud::AuthConfig d = c;
        d.host = "devvm48270.atn0.facebook.com:8080";
        api::agentcloud::resolve_web_origin(d, "");
        CHECK(!d.web_configured() && d.web_origin.empty());
        api::AgentcloudClient off(d, tok("t"));
        CHECK(!off.supports_inbox_state());
        auto r = off.read_inbox_state();
        CHECK(!r.ok && r.error.find("not addressable") != std::string::npos);
        auto w = off.write_snooze("s1", 9000);
        CHECK(!w.ok);
        api::agentcloud::resolve_web_origin(d, "127.0.0.1:1");
        CHECK(d.web_configured() && d.web_origin == "127.0.0.1:1" && d.web_verifier == is::kCatVerifier);
    }

    httplib::Server svr;
    std::string seen_cookie, seen_csrf, seen_ct, seen_body, seen_orch_header;
    std::atomic<int> mode{0};
    svr.Get(is::kRoutePath, [&](const httplib::Request& req, httplib::Response& res) {
        seen_cookie = req.get_header_value("Cookie");
        seen_orch_header = req.get_header_value("crypto_auth_tokens");
        switch (mode.load()) {
            case 0:
                res.set_content(R"({"read":[],"snoozes":{"t6":{"snoozedAt":1,"snoozedUntil":5}}})",
                                "application/json");
                break;
            case 1:
                res.status = 302;
                res.set_header("Location", "https://login.example/");
                break;
            case 2:
                res.set_content("<!doctype html><title>Log in</title>", "text/html; charset=utf-8");
                break;
            case 3: res.status = 403; break;
            default: res.status = 500; break;
        }
    });
    svr.Post(is::kRoutePath, [&](const httplib::Request& req, httplib::Response& res) {
        seen_cookie = req.get_header_value("Cookie");
        seen_csrf = req.get_header_value(is::kCsrfHeader);
        seen_ct = req.get_header_value("Content-Type");
        seen_body = req.body;
        switch (mode.load()) {
            case 0: res.set_content(R"({"ok":true,"snoozedAt":123,"snoozedUntil":9000})", "application/json"); break;
            case 5: res.status = 409; res.set_content(R"({"error":"cap"})", "application/json"); break;
            case 6: res.status = 403; break;
            default: res.status = 500; break;
        }
    });
    const int port = svr.bind_to_any_port("127.0.0.1");
    CHECK(port > 0);
    std::thread th([&] { svr.listen_after_bind(); });
    svr.wait_until_ready();

    api::agentcloud::AuthConfig cfg;
    cfg.proxy_host.clear();
    cfg.proxy_port = 0;
    cfg.mint_host = "unused";
    cfg.verifier = "orch-verifier";
    cfg.host = "127.0.0.1:1";
    api::agentcloud::resolve_web_origin(cfg, "127.0.0.1:" + std::to_string(port));
    api::AgentcloudClient client(cfg, tok("ORCH"));
    client.seed_web_token(tok("WEBCAT"));
    CHECK(client.supports_inbox_state());

    {
        auto r = client.read_inbox_state();
        CHECK(r.ok && r.value.http_status == 200 && !r.value.signed_out);
        CHECK(seen_cookie == std::string(is::kAuthCookieName) + "=WEBCAT");
        CHECK(seen_orch_header.empty());
        auto snap = is::parse_snapshot(r.value.body);
        CHECK(snap && snap->snoozed_until.count("t6") == 1);
    }
    {
        auto w = client.write_snooze("s1", 9000);
        CHECK(w.ok && w.value.http_status == 200);
        CHECK(seen_csrf == is::kCsrfHeaderValue);
        CHECK(seen_ct.rfind("application/json", 0) == 0);
        CHECK(seen_body == R"({"sessionId":"s1","snoozedUntil":9000})");
        auto echo = is::parse_echo(w.value.body);
        CHECK(echo && is::confirm_snooze(*echo, 9000).kind == is::Confirmation::Confirmed);
        w = client.write_snooze("s1", std::nullopt);
        CHECK(w.ok && seen_body == R"({"sessionId":"s1","snoozedUntil":null})");
        w = client.write_snooze("", 9000);
        CHECK(!w.ok && w.error.find("session id") != std::string::npos);
    }
    {
        mode = 1;
        auto r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 302 && r.value.signed_out);
        mode = 2;
        r = client.read_inbox_state();
        CHECK(r.ok && r.value.signed_out);
        CHECK(!is::parse_snapshot(r.value.body));
        mode = 3;
        r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 403 && r.value.signed_out);
        r = client.read_inbox_state();
        CHECK(!r.ok && r.error.find("mint") != std::string::npos);
        client.seed_web_token(tok("WEBCAT2"));
        mode = 4;
        r = client.read_inbox_state();
        CHECK(seen_cookie == std::string(is::kAuthCookieName) + "=WEBCAT2");
        CHECK(!r.ok && r.value.http_status == 500 && !r.value.signed_out);
    }
    {
        mode = 5;
        auto w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.value.http_status == 409);
        mode = 6;
        w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.value.http_status == 403);
        w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.error.find("mint") != std::string::npos);
    }

    svr.stop();
    th.join();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
