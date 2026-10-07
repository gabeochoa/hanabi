#pragma once

// Credential minting for the agentcloud orchestrator.
//
// There is no device-code flow here and no stored secret. On a managed machine
// a local forward proxy mints a short-lived token on demand, and possession of
// the device is the whole of the authentication — so sign-in is invisible and
// there is nothing for the app to persist or for a Settings pane to show.
//
// The mint is an ordinary plaintext GET aimed at a proxy-local pseudo-host, so
// it goes through cpp-httplib like everything else; only the session socket
// needs the Obj-C seam (ws_socket.h). It must NOT be tunnelled: CONNECT
// semantics would make TLS end-to-end and prevent the proxy from injecting the
// device identity that authenticates the call.
//
// NOTHING HERE IS HARDCODED, deliberately and per this repo's standing rule.
// The proxy, the pseudo-host, the verifier identity and the orchestrator host
// all come from the environment. Unset means "not configured", which is what
// keeps the mock the zero-config default rather than something that fails at
// startup on a machine with no proxy.
//
//   HANABI_AC_PROXY_HOST      default 127.0.0.1
//   HANABI_AC_PROXY_PORT      default 10054
//   HANABI_AC_MINT_HOST       proxy-local pseudo-host that mints (required)
//   HANABI_AC_VERIFIER        service identity to mint against (required)
//   HANABI_AC_HOST            orchestrator host for the session socket (required)
//   HANABI_AC_VALIDITY_SECS   default 10800 (3h)

#include <cstdint>
#include <mutex>
#include <string>

namespace api::agentcloud {

struct AuthConfig {
    std::string proxy_host = "127.0.0.1";
    int proxy_port = 10054;
    std::string mint_host;   // no default: internal, comes from env
    std::string verifier;    // no default: internal, comes from env
    std::string host;        // orchestrator, for ws:// — no default
    int validity_secs = 10800;
    // The client app identity (agentcloud spec 1137): the entitlement id of
    // the key this client attributes its calls to, sent as `app_id` on every
    // /ws/chat dial and as `x-agentcloud-app-id` on the web app's routes, the
    // way the reference sends its own. From HANABI_AC_APP_ID; digits only, and
    // empty sends nothing (the key string itself never goes on the wire).
    std::string app_id;

    // Every required field present. False means "run the mock".
    [[nodiscard]] bool configured() const {
        return !mint_host.empty() && !verifier.empty() && !host.empty();
    }

    std::string web_origin;
    std::string web_verifier;
    [[nodiscard]] bool web_configured() const {
        return configured() && !web_origin.empty() && !web_verifier.empty();
    }
    [[nodiscard]] AuthConfig web_auth() const {
        AuthConfig w = *this;
        w.verifier = web_verifier;
        w.host = web_origin;
        return w;
    }
};

void resolve_web_origin(AuthConfig& cfg, const std::string& override_origin);

// Reads the HANABI_AC_* environment above. Never throws; missing values simply
// leave the config unconfigured().
AuthConfig auth_config_from_env();

// A minted credential and the moment it stops being usable.
struct Token {
    std::string value;
    int64_t expires_at = 0;  // unix seconds
    [[nodiscard]] bool empty() const { return value.empty(); }
};

// Mints one token. Returns an empty Token on any failure — the caller decides
// whether that is fatal, because on a machine with no proxy it simply is not.
// `error` is filled with a human-readable reason when it fails.
Token mint_token(const AuthConfig& cfg, std::string* error);

// Caches a token and re-mints once it is inside the renew skew. The reference
// client uses five minutes; a token that expires mid-request costs a whole
// reconnect, so renewing early is much cheaper than renewing late.
class TokenCache {
   public:
    explicit TokenCache(AuthConfig cfg) : cfg_(std::move(cfg)) {}
    TokenCache(AuthConfig cfg, Token token)
        : cfg_(std::move(cfg)), token_(std::move(token)) {}

    // Cached token, minting if absent or near expiry. Empty on failure.
    Token get(std::string* error);

    // Drops the cached token so the next get() re-mints. Call when the server
    // rejects a token the client still believed was valid — the clocks can
    // disagree, and the server's opinion is the one that counts.
    void invalidate();

    void seed(Token token);

    [[nodiscard]] const AuthConfig& config() const { return cfg_; }

   private:
    mutable std::mutex mu_;
    AuthConfig cfg_;
    Token token_;
};

// Percent-encodes a query value. Exposed for the unit test: the verifier
// contains a colon, and getting that wrong is a 400 with an opaque body.
std::string percent_encode(const std::string& s);

inline constexpr const char* kAppIdHeader = "x-agentcloud-app-id";

// An entitlement id is a decimal number; anything else is not sent.
inline bool is_app_id(const std::string& s) {
    if (s.empty() || s.size() > 20) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// The orchestrator's chat socket for this config, carrying the app identity
// when one is set.
inline std::string ws_chat_url(const AuthConfig& cfg) {
    std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    if (is_app_id(cfg.app_id)) url += "&app_id=" + cfg.app_id;
    return url;
}

}  // namespace api::agentcloud
