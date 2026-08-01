#include "client.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../../vendor/nlohmann/json.hpp"
#include "http_client.h"
#include "mock_client.h"

namespace api {

namespace {
std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v && *v) return std::string(v);
    return fallback;
}

// Resolve the config file path. Priority:
//   1. $HANABI_CONFIG (explicit path)
//   2. $XDG_CONFIG_HOME/hanabi/config.json
//   3. $HOME/.config/hanabi/config.json
std::string config_file_path() {
    if (const char* explicit_path = std::getenv("HANABI_CONFIG");
        explicit_path && *explicit_path) {
        return explicit_path;
    }
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return (fs::path(xdg) / "hanabi" / "config.json").string();
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return (fs::path(home) / ".config" / "hanabi" / "config.json").string();
    }
    return "";
}

// Load config values from the JSON file if it exists. Missing file / bad JSON
// is NOT an error — we simply keep the passed-in defaults (env still applies
// on top). Keys mirror the env var names, lowercased without the HANABI_
// prefix: backend, api_base_url, token, sessions_path, messages_path, and the
// field_* names. Unknown keys are ignored.
void load_config_file(Config& c) {
    const std::string path = config_file_path();
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in.good()) return;
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return;  // malformed file: ignore, fall back to defaults + env
    }
    if (!j.is_object()) return;
    auto str = [&](const char* key, std::string& dst) {
        if (j.contains(key) && j.at(key).is_string())
            dst = j.at(key).get<std::string>();
    };
    str("backend", c.backend);
    str("api_base_url", c.base_url);
    str("base_url", c.base_url);  // accept either spelling
    str("token", c.token);
    str("sessions_path", c.sessions_path);
    str("messages_path", c.messages_path);
    str("chat_path", c.chat_path);
    str("field_id", c.field_id);
    str("field_title", c.field_title);
    str("field_updated_at", c.field_updated_at);
    str("field_status", c.field_status);
    str("field_preview", c.field_preview);
    str("field_messages", c.field_messages);
    str("field_role", c.field_role);
    str("field_text", c.field_text);
    str("field_created_at", c.field_created_at);
    str("field_blocks", c.field_blocks);
    str("field_block_type", c.field_block_type);
    str("field_block_content", c.field_block_content);
    str("field_block_text_type", c.field_block_text_type);
    str("field_prompt", c.field_prompt);
    str("field_session_id", c.field_session_id);
    // Device-code auth (Phase AUTH). All optional; empty endpoint paths keep
    // auth OFF.
    str("auth_device_path", c.auth_device_path);
    str("auth_token_path", c.auth_token_path);
    str("auth_client_id", c.auth_client_id);
    str("auth_scope", c.auth_scope);
    str("field_device_code", c.field_device_code);
    str("field_user_code", c.field_user_code);
    str("field_verification_uri", c.field_verification_uri);
    str("field_interval", c.field_interval);
    str("field_expires_in", c.field_expires_in);
    str("field_access_token", c.field_access_token);
    str("field_refresh_token", c.field_refresh_token);
    str("field_auth_error", c.field_auth_error);
    str("auth_pending_value", c.auth_pending_value);
}
}  // namespace

Config Config::from_env() {
    Config c;

    // Layer 1: load a config file (if present) over the built-in defaults.
    load_config_file(c);

    // Layer 2: environment variables OVERRIDE the file (so a one-off export can
    // point at a different backend without editing the file). The env fallback
    // for each is the value the file (or the default) already supplied.
    c.backend = env_or("HANABI_BACKEND", c.backend);

    // Base URL: prefer the descriptive HANABI_API_BASE_URL (documented name);
    // fall back to the shorter HANABI_BASE_URL for backward compatibility.
    c.base_url = env_or("HANABI_API_BASE_URL", env_or("HANABI_BASE_URL", c.base_url));
    c.token = env_or("HANABI_TOKEN", c.token);
    c.sessions_path = env_or("HANABI_SESSIONS_PATH", c.sessions_path);
    c.messages_path = env_or("HANABI_MESSAGES_PATH", c.messages_path);
    // Chat/send path (Phase SEND). Empty by default => http send is opt-in and
    // an unconfigured http backend honestly reports it can't reply.
    c.chat_path = env_or("HANABI_CHAT_PATH", c.chat_path);

    // Field-name overrides let the generic adapter match different response
    // shapes without recompiling.
    c.field_id = env_or("HANABI_FIELD_ID", c.field_id);
    c.field_title = env_or("HANABI_FIELD_TITLE", c.field_title);
    c.field_updated_at = env_or("HANABI_FIELD_UPDATED_AT", c.field_updated_at);
    c.field_status = env_or("HANABI_FIELD_STATUS", c.field_status);
    c.field_preview = env_or("HANABI_FIELD_PREVIEW", c.field_preview);
    c.field_messages = env_or("HANABI_FIELD_MESSAGES", c.field_messages);
    c.field_role = env_or("HANABI_FIELD_ROLE", c.field_role);
    c.field_text = env_or("HANABI_FIELD_TEXT", c.field_text);
    c.field_created_at = env_or("HANABI_FIELD_CREATED_AT", c.field_created_at);

    // Chat/send request field names (Phase SEND). Generic defaults.
    c.field_prompt = env_or("HANABI_FIELD_PROMPT", c.field_prompt);
    c.field_session_id =
        env_or("HANABI_FIELD_SESSION_ID", c.field_session_id);

    // Block-array transcript shape (optional; defaults match a common
    // blocks:[{type,content}] layout, ignored when the array is absent).
    c.field_blocks = env_or("HANABI_FIELD_BLOCKS", c.field_blocks);
    c.field_block_type = env_or("HANABI_FIELD_BLOCK_TYPE", c.field_block_type);
    c.field_block_content =
        env_or("HANABI_FIELD_BLOCK_CONTENT", c.field_block_content);
    c.field_block_text_type =
        env_or("HANABI_FIELD_BLOCK_TEXT_TYPE", c.field_block_text_type);

    // Device-code auth (Phase AUTH). Endpoint paths default EMPTY, so auth is
    // opt-in: with neither set, auth_ready() is false and the app behaves
    // exactly as before. No real endpoint/client_id is baked in anywhere.
    c.auth_device_path = env_or("HANABI_AUTH_DEVICE_PATH", c.auth_device_path);
    c.auth_token_path = env_or("HANABI_AUTH_TOKEN_PATH", c.auth_token_path);
    c.auth_client_id = env_or("HANABI_AUTH_CLIENT_ID", c.auth_client_id);
    c.auth_scope = env_or("HANABI_AUTH_SCOPE", c.auth_scope);
    c.field_device_code =
        env_or("HANABI_FIELD_DEVICE_CODE", c.field_device_code);
    c.field_user_code = env_or("HANABI_FIELD_USER_CODE", c.field_user_code);
    c.field_verification_uri =
        env_or("HANABI_FIELD_VERIFICATION_URI", c.field_verification_uri);
    c.field_interval = env_or("HANABI_FIELD_INTERVAL", c.field_interval);
    c.field_expires_in = env_or("HANABI_FIELD_EXPIRES_IN", c.field_expires_in);
    c.field_access_token =
        env_or("HANABI_FIELD_ACCESS_TOKEN", c.field_access_token);
    c.field_refresh_token =
        env_or("HANABI_FIELD_REFRESH_TOKEN", c.field_refresh_token);
    c.field_auth_error = env_or("HANABI_FIELD_AUTH_ERROR", c.field_auth_error);
    c.auth_pending_value =
        env_or("HANABI_AUTH_PENDING_VALUE", c.auth_pending_value);

    return c;
}

std::unique_ptr<Client> make_client(const Config& cfg) {
    if (cfg.backend == "http" && cfg.http_ready())
        return std::make_unique<HttpClient>(cfg);
    // Default and safe fallback: fully offline sample data.
    return std::make_unique<MockClient>();
}

}  // namespace api
