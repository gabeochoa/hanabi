#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace api::mock_journal {

inline constexpr const char* kPathEnv = "HANABI_MOCK_JOURNAL";

struct Entry {
    std::string session_id;
    std::string kind;
    std::string text;
    std::size_t attachments = 0;
    std::int64_t at_unix_ms = 0;
    std::string local_id;
};

inline std::string path_from_env() {
    const char* v = std::getenv(kPathEnv);
    return (v == nullptr || *v == '\0') ? std::string() : std::string(v);
}

inline bool enabled() { return !path_from_env().empty(); }

inline nlohmann::json to_json(const Entry& e) {
    nlohmann::json j{{"session_id", e.session_id},
                     {"kind", e.kind},
                     {"text", e.text},
                     {"attachments", e.attachments},
                     {"at_unix_ms", e.at_unix_ms}};
    if (!e.local_id.empty()) j["local_id"] = e.local_id;
    return j;
}

inline std::string line_for(const Entry& e) { return to_json(e).dump() + "\n"; }

inline std::mutex& mu() {
    static auto* m = new std::mutex;
    return *m;
}

inline bool append(const Entry& e, const std::string& path) {
    if (path.empty()) return false;
    std::lock_guard<std::mutex> lock(mu());
    std::ofstream out(path, std::ios::app);
    if (!out.good()) return false;
    out << line_for(e);
    return out.good();
}

inline bool append(const Entry& e) { return append(e, path_from_env()); }

inline bool clear(const std::string& path) {
    if (path.empty()) return false;
    std::lock_guard<std::mutex> lock(mu());
    std::ofstream out(path, std::ios::trunc);
    return out.good();
}

inline bool clear() { return clear(path_from_env()); }

inline std::int64_t now_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}
