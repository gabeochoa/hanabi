#include "client.h"

#include <cstdlib>

#include "http_client.h"
#include "mock_client.h"

namespace api {

namespace {
std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v && *v) return std::string(v);
    return fallback;
}
}  // namespace

Config Config::from_env() {
    Config c;
    c.backend = env_or("HANABI_BACKEND", "mock");

    // Base URL: prefer the descriptive HANABI_API_BASE_URL (documented name);
    // fall back to the shorter HANABI_BASE_URL for backward compatibility.
    c.base_url = env_or("HANABI_API_BASE_URL", env_or("HANABI_BASE_URL", ""));
    c.token = env_or("HANABI_TOKEN", "");
    c.sessions_path = env_or("HANABI_SESSIONS_PATH", c.sessions_path);
    c.messages_path = env_or("HANABI_MESSAGES_PATH", c.messages_path);

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

    return c;
}

std::unique_ptr<Client> make_client(const Config& cfg) {
    if (cfg.backend == "http" && cfg.http_ready())
        return std::make_unique<HttpClient>(cfg);
    // Default and safe fallback: fully offline sample data.
    return std::make_unique<MockClient>();
}

}  // namespace api
