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
    str("web_base_url", c.web_base_url);
    str("token", c.token);
    str("sessions_path", c.sessions_path);
    str("messages_path", c.messages_path);
    str("chat_path", c.chat_path);
    // Agent steering (Phase STEER). Optional; empty steer_path keeps http
    // steering OFF (the app always sends normally). Origin-absolute "//path"
    // is honored (skips the base prefix) exactly like chat_path.
    str("steer_path", c.steer_path);
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
    // Streaming (Phase STREAM). All optional; empty stream_path keeps http
    // streaming OFF (the app falls back to the synchronous send path).
    str("stream_path", c.stream_path);
    str("field_event_type", c.field_event_type);
    str("field_event_text", c.field_event_text);
    str("field_event_title", c.field_event_title);
    str("event_type_text", c.event_type_text);
    str("event_type_thinking", c.event_type_thinking);
    str("event_type_tool_call", c.event_type_tool_call);
    str("event_type_done", c.event_type_done);
    str("event_type_title_update", c.event_type_title_update);
    // User settings / config read (feature #4). Optional; settings_path
    // defaults to "/whoami" (the reachable real endpoint on the real backend).
    str("settings_path", c.settings_path);
    str("field_settings_user_id", c.field_settings_user_id);
    str("field_settings_bank_id", c.field_settings_bank_id);
    str("field_settings_counts", c.field_settings_counts);
    str("field_settings_sessions", c.field_settings_sessions);
    str("field_settings_assets", c.field_settings_assets);
    str("field_settings_schedules", c.field_settings_schedules);
    str("field_settings_skills", c.field_settings_skills);
    // Device-code auth (Phase AUTH). All optional; empty endpoint paths keep
    // auth OFF.
    str("auth_device_path", c.auth_device_path);
    str("auth_token_path", c.auth_token_path);
    str("auth_refresh_path", c.auth_refresh_path);
    str("auth_base_url", c.auth_base_url);
    str("auth_client_type", c.auth_client_type);
    str("field_device_code", c.field_device_code);
    str("field_client_type", c.field_client_type);
    str("field_poll_query", c.field_poll_query);
    str("field_user_code", c.field_user_code);
    str("field_auth_url", c.field_auth_url);
    str("field_auth_status", c.field_auth_status);
    str("field_token", c.field_token);
    str("field_refresh_token", c.field_refresh_token);
    str("auth_status_pending", c.auth_status_pending);
    str("auth_status_authorized", c.auth_status_authorized);
    auto num = [&](const char* key, int64_t& dst) {
        if (j.contains(key) && j.at(key).is_number_integer())
            dst = j.at(key).get<int64_t>();
    };
    num("auth_poll_interval", c.auth_poll_interval);
    num("auth_expires_in", c.auth_expires_in);
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
    c.web_base_url = env_or("HANABI_WEB_BASE_URL", c.web_base_url);
    c.token = env_or("HANABI_TOKEN", c.token);
    c.sessions_path = env_or("HANABI_SESSIONS_PATH", c.sessions_path);
    c.messages_path = env_or("HANABI_MESSAGES_PATH", c.messages_path);
    // Chat/send path (Phase SEND). Empty by default => http send is opt-in and
    // an unconfigured http backend honestly reports it can't reply.
    c.chat_path = env_or("HANABI_CHAT_PATH", c.chat_path);

    // Steer path (Phase STEER). Empty by default => http steering is opt-in;
    // when set (e.g. "//api/…"), a send into a Running thread interrupts the
    // in-flight turn instead of starting a fresh one. Nothing endpoint-specific
    // is baked in — the local config chooses the path.
    c.steer_path = env_or("HANABI_STEER_PATH", c.steer_path);

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

    // Streaming (Phase STREAM). Stream path defaults EMPTY => http streaming is
    // opt-in; the mock streams unconditionally. Event field names + type values
    // are generic + overridable, exactly like the field_* mapping above.
    c.stream_path = env_or("HANABI_STREAM_PATH", c.stream_path);
    c.field_event_type = env_or("HANABI_FIELD_EVENT_TYPE", c.field_event_type);
    c.field_event_text = env_or("HANABI_FIELD_EVENT_TEXT", c.field_event_text);
    c.field_event_title =
        env_or("HANABI_FIELD_EVENT_TITLE", c.field_event_title);
    c.event_type_text = env_or("HANABI_EVENT_TYPE_TEXT", c.event_type_text);
    c.event_type_thinking =
        env_or("HANABI_EVENT_TYPE_THINKING", c.event_type_thinking);
    c.event_type_tool_call =
        env_or("HANABI_EVENT_TYPE_TOOL_CALL", c.event_type_tool_call);
    c.event_type_done = env_or("HANABI_EVENT_TYPE_DONE", c.event_type_done);
    c.event_type_title_update =
        env_or("HANABI_EVENT_TYPE_TITLE_UPDATE", c.event_type_title_update);

    // User settings / config read (feature #4). settings_path defaults to
    // "/whoami"; the field_settings_* names map its response onto UserSettings.
    c.settings_path = env_or("HANABI_SETTINGS_PATH", c.settings_path);
    c.field_settings_user_id =
        env_or("HANABI_FIELD_SETTINGS_USER_ID", c.field_settings_user_id);
    c.field_settings_bank_id =
        env_or("HANABI_FIELD_SETTINGS_BANK_ID", c.field_settings_bank_id);
    c.field_settings_counts =
        env_or("HANABI_FIELD_SETTINGS_COUNTS", c.field_settings_counts);
    c.field_settings_sessions =
        env_or("HANABI_FIELD_SETTINGS_SESSIONS", c.field_settings_sessions);
    c.field_settings_assets =
        env_or("HANABI_FIELD_SETTINGS_ASSETS", c.field_settings_assets);
    c.field_settings_schedules =
        env_or("HANABI_FIELD_SETTINGS_SCHEDULES", c.field_settings_schedules);
    c.field_settings_skills =
        env_or("HANABI_FIELD_SETTINGS_SKILLS", c.field_settings_skills);

    // Block-array transcript shape (optional; defaults match a common
    // blocks:[{type,content}] layout, ignored when the array is absent).
    c.field_blocks = env_or("HANABI_FIELD_BLOCKS", c.field_blocks);
    c.field_block_type = env_or("HANABI_FIELD_BLOCK_TYPE", c.field_block_type);
    c.field_block_content =
        env_or("HANABI_FIELD_BLOCK_CONTENT", c.field_block_content);
    c.field_block_text_type =
        env_or("HANABI_FIELD_BLOCK_TEXT_TYPE", c.field_block_text_type);

    // Device-code auth (Phase AUTH). The REAL navi-CLI flow. Endpoint paths
    // default to the generic navi-CLI paths (siblings of the API, hence the
    // separate auth ORIGIN); with an unconfigured backend (no base_url) or a
    // static/stored token, auth_ready()/the main.cpp gate keep auth OFF. No
    // real HOST or client_id/secret is baked in anywhere.
    c.auth_device_path =
        env_or("HANABI_AUTH_DEVICE_PATH", c.auth_device_path);
    c.auth_token_path = env_or("HANABI_AUTH_TOKEN_PATH", c.auth_token_path);
    c.auth_refresh_path =
        env_or("HANABI_AUTH_REFRESH_PATH", c.auth_refresh_path);
    c.auth_base_url = env_or("HANABI_AUTH_BASE_URL", c.auth_base_url);
    c.auth_client_type =
        env_or("HANABI_AUTH_CLIENT_TYPE", c.auth_client_type);
    c.field_device_code =
        env_or("HANABI_FIELD_DEVICE_CODE", c.field_device_code);
    c.field_client_type =
        env_or("HANABI_FIELD_CLIENT_TYPE", c.field_client_type);
    c.field_poll_query =
        env_or("HANABI_FIELD_POLL_QUERY", c.field_poll_query);
    c.field_user_code = env_or("HANABI_FIELD_USER_CODE", c.field_user_code);
    c.field_auth_url = env_or("HANABI_FIELD_AUTH_URL", c.field_auth_url);
    c.field_auth_status =
        env_or("HANABI_FIELD_AUTH_STATUS", c.field_auth_status);
    c.field_token = env_or("HANABI_FIELD_TOKEN", c.field_token);
    c.field_refresh_token =
        env_or("HANABI_FIELD_REFRESH_TOKEN", c.field_refresh_token);
    c.auth_status_pending =
        env_or("HANABI_AUTH_STATUS_PENDING", c.auth_status_pending);
    c.auth_status_authorized =
        env_or("HANABI_AUTH_STATUS_AUTHORIZED", c.auth_status_authorized);
    if (const char* v = std::getenv("HANABI_AUTH_POLL_INTERVAL"); v && *v) {
        try { c.auth_poll_interval = std::stoll(v); } catch (...) {}
    }
    if (const char* v = std::getenv("HANABI_AUTH_EXPIRES_IN"); v && *v) {
        try { c.auth_expires_in = std::stoll(v); } catch (...) {}
    }

    return c;
}

std::unique_ptr<Client> make_client(const Config& cfg) {
    if (cfg.backend == "http" && cfg.http_ready())
        return std::make_unique<HttpClient>(cfg);
    // Default and safe fallback: fully offline sample data.
    return std::make_unique<MockClient>();
}

}  // namespace api
