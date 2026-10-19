#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::draft_key {

inline bool is_pseudo(const std::string& id) { return id.empty() || id.rfind("__", 0) == 0; }

inline std::string thread_key(const std::string& id) { return std::string("t\x1f") + id; }

inline std::optional<std::string> modern_of_legacy(const std::string& key) {
    constexpr const char* kPrefix = "reply:";
    const std::size_t prefix = 6;
    if (key.size() <= prefix + 2 || key.compare(0, prefix, kPrefix) != 0) return std::nullopt;
    std::size_t at = prefix;
    while (at < key.size() && std::isdigit(static_cast<unsigned char>(key[at]))) ++at;
    if (at == prefix || at >= key.size() || key[at] != '\x1f') return std::nullopt;
    const std::string id = key.substr(at + 1);
    if (is_pseudo(id)) return std::nullopt;
    return std::string(kPrefix) + thread_key(id);
}

inline int migrate_legacy(nlohmann::json& drafts) {
    if (!drafts.is_object()) return 0;
    std::vector<std::pair<std::string, std::string>> moves;
    for (const auto& [key, value] : drafts.items()) {
        (void)value;
        if (auto modern = modern_of_legacy(key)) moves.emplace_back(key, *modern);
    }
    int moved = 0;
    for (const auto& [legacy, modern] : moves) {
        nlohmann::json entry = drafts[legacy];
        drafts.erase(legacy);
        if (!drafts.contains(modern)) {
            drafts[modern] = std::move(entry);
            ++moved;
        }
    }
    return moved;
}

}  // namespace api::draft_key
