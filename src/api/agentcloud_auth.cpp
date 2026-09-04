#include "agentcloud_auth.h"

#include <cctype>
#include <cstdlib>
#include <ctime>

// Mirror http_client.cpp's guard exactly. The mint itself is plaintext and
// wants no TLS at all, but httplib is header-only: defining this in one TU and
// not another changes its types and quietly violates the ODR.
#ifdef HANABI_ENABLE_TLS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace api::agentcloud {
namespace {

// Reads an env var, or returns the fallback when unset or empty. Empty is
// treated as unset so `FOO= ./hanabi` reads as "leave it alone" rather than
// "set it to nothing", which is what anyone typing that means.
std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') return fallback;
    return std::string(v);
}

int env_int_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(v, &end, 10);
    // A non-numeric or out-of-range value is a typo, not an instruction to
    // pick something arbitrary — keep the default rather than dial a port 0.
    if (end == v || parsed <= 0 || parsed > 2147483647L) return fallback;
    return static_cast<int>(parsed);
}

}  // namespace

std::string percent_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        // RFC 3986 unreserved set. Everything else is escaped, which for the
        // verifier means the colon in SERVICE_IDENTITY:<name> becomes %3A.
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

AuthConfig auth_config_from_env() {
    AuthConfig cfg;
    cfg.proxy_host = env_or("HANABI_AC_PROXY_HOST", cfg.proxy_host);
    cfg.proxy_port = env_int_or("HANABI_AC_PROXY_PORT", cfg.proxy_port);
    cfg.mint_host = env_or("HANABI_AC_MINT_HOST", "");
    cfg.verifier = env_or("HANABI_AC_VERIFIER", "");
    cfg.host = env_or("HANABI_AC_HOST", "");
    cfg.validity_secs = env_int_or("HANABI_AC_VALIDITY_SECS", cfg.validity_secs);
    return cfg;
}

Token mint_token(const AuthConfig& cfg, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) *error = why;
        return Token{};
    };

    if (!cfg.configured())
        return fail("agentcloud not configured (HANABI_AC_* unset)");

    // Plain http:// to the pseudo-host, forwarded by the local proxy. Not
    // https: a CONNECT tunnel would hide the request from the proxy, and the
    // proxy injecting device identity is the entire authentication.
    httplib::Client cli(("http://" + cfg.mint_host).c_str());
    cli.set_proxy(cfg.proxy_host.c_str(), cfg.proxy_port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(15, 0);

    const std::string path = "/create?lowbox=true&verifiers=" +
                             percent_encode(cfg.verifier) +
                             "&validity_period=" +
                             std::to_string(cfg.validity_secs);

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    auto res = cli.Get(path.c_str());
    if (!res)
        return fail("mint unreachable via proxy " + cfg.proxy_host + ":" +
                    std::to_string(cfg.proxy_port));
    if (res->status != 200)
        return fail("mint returned HTTP " + std::to_string(res->status));
    if (res->body.empty()) return fail("mint returned an empty body");

    // The response body IS the serialized credential — no JSON envelope, so
    // there is nothing to parse and nothing to get wrong.
    Token t;
    t.value = res->body;
    t.expires_at = now + cfg.validity_secs;
    return t;
}

Token TokenCache::get(std::string* error) {
    std::lock_guard<std::mutex> lk(mu_);
    // Renew inside a five-minute skew. Expiring mid-request costs a full
    // reconnect, so early is much cheaper than late.
    constexpr int64_t kRenewSkewSecs = 300;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (!token_.empty() && token_.expires_at - kRenewSkewSecs > now)
        return token_;

    Token fresh = mint_token(cfg_, error);
    if (!fresh.empty()) token_ = fresh;
    return fresh;
}

void TokenCache::invalidate() {
    std::lock_guard<std::mutex> lk(mu_);
    token_ = Token{};
}

}  // namespace api::agentcloud
