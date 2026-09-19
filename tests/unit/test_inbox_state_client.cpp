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

static constexpr const char* kMintHost = "mint.test";
static constexpr const char* kOrchHost = "orch.test:1";

struct Fixture {
    httplib::Server svr;
    std::thread th;
    int port = 0;
    std::string web_origin;

    std::atomic<int> web_mode{0};
    std::atomic<int> mint_mode{0};
    std::atomic<int> mint_serial{0};
    std::atomic<int> reads{0}, writes{0}, mints{0}, unrouted{0};
    std::string seen_cookie, seen_csrf, seen_ct, seen_body, seen_orch_header;
    std::string mint_target, mint_verifier, mint_lowbox, mint_validity, last_minted;

    static bool is_absolute_for(const httplib::Request& req, const std::string& host_port) {
        return req.target.rfind("http://" + host_port + "/", 0) == 0;
    }

    void start() {
        svr.Get(R"(^(http://[^/]+)?/create$)", [&](const httplib::Request& req, httplib::Response& res) {
            ++mints;
            mint_target = req.target;
            mint_verifier = req.get_param_value("verifiers");
            mint_lowbox = req.get_param_value("lowbox");
            mint_validity = req.get_param_value("validity_period");
            switch (mint_mode.load()) {
                case 0:
                    last_minted = "MINTED-" + std::to_string(++mint_serial);
                    res.set_content(last_minted, "text/plain");
                    break;
                case 1: res.status = 503; break;
                default: res.set_content("", "text/plain"); break;
            }
        });
        svr.Get(std::string("^(http://[^/]+)?") + api::inbox_state::kRoutePath + "$",
                [&](const httplib::Request& req, httplib::Response& res) {
                    ++reads;
                    if (!is_absolute_for(req, web_origin)) ++unrouted;
                    seen_cookie = req.get_header_value("Cookie");
                    seen_orch_header = req.get_header_value("crypto_auth_tokens");
                    switch (web_mode.load()) {
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
                        case 7: res.status = 401; break;
                        default: res.status = 500; break;
                    }
                });
        svr.Post(std::string("^(http://[^/]+)?") + api::inbox_state::kRoutePath + "$",
                 [&](const httplib::Request& req, httplib::Response& res) {
                     ++writes;
                     if (!is_absolute_for(req, web_origin)) ++unrouted;
                     seen_cookie = req.get_header_value("Cookie");
                     seen_csrf = req.get_header_value(api::inbox_state::kCsrfHeader);
                     seen_ct = req.get_header_value("Content-Type");
                     seen_body = req.body;
                     switch (web_mode.load()) {
                         case 0:
                             res.set_content(R"({"ok":true,"snoozedAt":123,"snoozedUntil":9000})", "application/json");
                             break;
                         case 5: res.status = 409; res.set_content(R"({"error":"cap"})", "application/json"); break;
                         case 6: res.status = 403; break;
                         default: res.status = 500; break;
                     }
                 });
        svr.set_error_handler([&](const httplib::Request&, httplib::Response& res) {
            if (res.status != 404) return httplib::Server::HandlerResponse::Handled;
            ++unrouted;
            res.status = 599;
            return httplib::Server::HandlerResponse::Handled;
        });
        port = svr.bind_to_any_port("127.0.0.1");
        web_origin = "127.0.0.1:" + std::to_string(port);
        th = std::thread([&] { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }
    void stop() {
        svr.stop();
        th.join();
    }
    std::string cookie_for(const std::string& token) const {
        return std::string(api::inbox_state::kAuthCookieName) + "=" + token;
    }
};

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

    Fixture fx;
    fx.start();
    CHECK(fx.port > 0);

    api::agentcloud::AuthConfig cfg;
    cfg.proxy_host = "127.0.0.1";
    cfg.proxy_port = fx.port;
    cfg.mint_host = kMintHost;
    cfg.verifier = "orch-verifier";
    cfg.host = kOrchHost;
    api::agentcloud::resolve_web_origin(cfg, fx.web_origin);
    api::AgentcloudClient client(cfg, tok("ORCH"));
    client.seed_web_token(tok("WEBCAT"));
    CHECK(client.supports_inbox_state());

    {
        auto r = client.read_inbox_state();
        CHECK(r.ok && r.value.http_status == 200 && !r.value.signed_out);
        CHECK(fx.seen_cookie == fx.cookie_for("WEBCAT"));
        CHECK(fx.seen_orch_header.empty());
        auto snap = is::parse_snapshot(r.value.body);
        CHECK(snap && snap->snoozed_until.count("t6") == 1);
        CHECK(fx.reads == 1 && fx.writes == 0 && fx.mints == 0 && fx.unrouted == 0);
    }
    {
        auto w = client.write_snooze("s1", 9000);
        CHECK(w.ok && w.value.http_status == 200);
        CHECK(fx.seen_csrf == is::kCsrfHeaderValue);
        CHECK(fx.seen_ct.rfind("application/json", 0) == 0);
        CHECK(fx.seen_body == R"({"sessionId":"s1","snoozedUntil":9000})");
        auto echo = is::parse_echo(w.value.body);
        CHECK(echo && is::confirm_snooze(*echo, 9000).kind == is::Confirmation::Confirmed);
        w = client.write_snooze("s1", std::nullopt);
        CHECK(w.ok && fx.seen_body == R"({"sessionId":"s1","snoozedUntil":null})");
        w = client.write_snooze("", 9000);
        CHECK(!w.ok && w.error.find("session id") != std::string::npos);
        CHECK(fx.reads == 1 && fx.writes == 2 && fx.mints == 0 && fx.unrouted == 0);
    }
    {
        fx.web_mode = 1;
        auto r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 302 && r.value.signed_out);
        fx.web_mode = 2;
        r = client.read_inbox_state();
        CHECK(r.ok && r.value.signed_out);
        CHECK(!is::parse_snapshot(r.value.body));
        CHECK(fx.reads == 3 && fx.mints == 0);
        CHECK(fx.seen_cookie == fx.cookie_for("WEBCAT"));
    }
    {
        fx.web_mode = 3;
        auto r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 403 && r.value.signed_out);
        CHECK(fx.reads == 4 && fx.mints == 0);

        fx.mint_mode = 1;
        r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 0);
        CHECK(r.error == "mint returned HTTP 503");
        CHECK(fx.mints == 1 && fx.reads == 4);
        CHECK(fx.mint_target == std::string("http://") + kMintHost + "/create?lowbox=true&verifiers=" +
                                    is::kCatVerifier + "&validity_period=" + std::to_string(cfg.validity_secs));
        CHECK(fx.mint_verifier == is::kCatVerifier);
        CHECK(fx.mint_lowbox == "true");
        CHECK(fx.mint_validity == std::to_string(cfg.validity_secs));

        fx.mint_mode = 2;
        r = client.read_inbox_state();
        CHECK(!r.ok && r.error == "mint returned an empty body");
        CHECK(fx.mints == 2 && fx.reads == 4);

        fx.mint_mode = 0;
        fx.web_mode = 0;
        r = client.read_inbox_state();
        CHECK(r.ok && r.value.http_status == 200 && !r.value.signed_out);
        CHECK(fx.mints == 3 && fx.reads == 5);
        CHECK(fx.last_minted == "MINTED-1");
        CHECK(fx.seen_cookie == fx.cookie_for(fx.last_minted));
        CHECK(fx.seen_orch_header.empty());

        r = client.read_inbox_state();
        CHECK(r.ok && fx.mints == 3 && fx.reads == 6);
        CHECK(fx.seen_cookie == fx.cookie_for("MINTED-1"));

        fx.web_mode = 7;
        r = client.read_inbox_state();
        CHECK(!r.ok && r.value.http_status == 401 && r.value.signed_out);
        CHECK(fx.mints == 3 && fx.reads == 7);
        fx.web_mode = 0;
        r = client.read_inbox_state();
        CHECK(r.ok && fx.mints == 4 && fx.reads == 8);
        CHECK(fx.seen_cookie == fx.cookie_for("MINTED-2"));

        client.seed_web_token(tok("WEBCAT2"));
        fx.web_mode = 4;
        r = client.read_inbox_state();
        CHECK(fx.seen_cookie == fx.cookie_for("WEBCAT2"));
        CHECK(!r.ok && r.value.http_status == 500 && !r.value.signed_out);
        r = client.read_inbox_state();
        CHECK(fx.seen_cookie == fx.cookie_for("WEBCAT2"));
        CHECK(fx.mints == 4 && fx.reads == 10);
    }
    {
        fx.web_mode = 5;
        auto w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.value.http_status == 409);
        CHECK(fx.seen_cookie == fx.cookie_for("WEBCAT2"));
        fx.web_mode = 6;
        w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.value.http_status == 403);
        CHECK(fx.mints == 4 && fx.writes == 4);

        fx.mint_mode = 1;
        w = client.write_snooze("s1", 9000);
        CHECK(!w.ok && w.value.http_status == 0);
        CHECK(w.error == "mint returned HTTP 503");
        CHECK(fx.mints == 5 && fx.writes == 4);

        fx.mint_mode = 0;
        fx.web_mode = 0;
        w = client.write_snooze("s1", 9000);
        CHECK(w.ok && w.value.http_status == 200);
        CHECK(fx.mints == 6 && fx.writes == 5);
        CHECK(fx.seen_cookie == fx.cookie_for("MINTED-3"));
        CHECK(fx.seen_csrf == is::kCsrfHeaderValue);
    }
    CHECK(fx.unrouted == 0);

    fx.stop();
    std::printf("routes: reads=%d writes=%d mints=%d unrouted=%d\n", fx.reads.load(), fx.writes.load(),
                fx.mints.load(), fx.unrouted.load());
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
