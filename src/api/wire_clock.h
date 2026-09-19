#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::wire_clock {

using Millis = std::int64_t;

inline std::optional<Millis> read_ms(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const std::uint64_t u = value.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(INT64_MAX)) return std::nullopt;
        return static_cast<Millis>(u);
    }
    if (value.is_number_integer()) {
        const std::int64_t v = value.get<std::int64_t>();
        if (v < 0) return std::nullopt;
        return v;
    }
    return std::nullopt;
}

inline std::optional<Millis> read_ms(const nlohmann::json& row, const char* key) {
    if (!row.is_object()) return std::nullopt;
    const auto it = row.find(key);
    if (it == row.end()) return std::nullopt;
    return read_ms(*it);
}

inline constexpr const char* kActivityKey = "last_activity_unix_ms";
inline constexpr const char* kEventKey = "last_event_unix_ms";
inline constexpr const char* kRunCompleteKey = "last_run_complete_unix_ms";

inline std::optional<Millis> read_event_ms(const nlohmann::json& row) {
    if (const auto activity = read_ms(row, kActivityKey)) return activity;
    return read_ms(row, kEventKey);
}

inline std::optional<Millis> read_run_complete_ms(const nlohmann::json& row) {
    return read_ms(row, kRunCompleteKey);
}

struct Clocks {
    std::optional<Millis> event_ms;
    std::optional<Millis> run_complete_ms;
    bool operator==(const Clocks&) const = default;
};

inline Clocks carry_held(Clocks fresh, const Clocks* held) {
    if (!fresh.event_ms && held != nullptr && held->event_ms) fresh.event_ms = held->event_ms;
    return fresh;
}

struct SeededClocks {
    bool found = false;
    Clocks clocks;
    bool operator==(const SeededClocks&) const = default;
};

inline std::optional<Millis> parse_seed_ms(std::string_view text) {
    if (text.empty() || text.size() > 19) return std::nullopt;
    Millis value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        if (value > (INT64_MAX - (c - '0')) / 10) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

inline SeededClocks seeded_clocks_for(std::string_view spec, std::string_view id, std::int64_t poll = 1) {
    SeededClocks out;
    std::int64_t bestFrom = -1;
    while (!spec.empty()) {
        const auto comma = spec.find(',');
        std::string_view item = spec.substr(0, comma);
        spec = comma == std::string_view::npos ? std::string_view() : spec.substr(comma + 1);
        std::int64_t from = 1;
        if (const auto at = item.rfind('@'); at != std::string_view::npos) {
            const auto parsed = parse_seed_ms(item.substr(at + 1));
            if (!parsed) continue;
            from = *parsed;
            item = item.substr(0, at);
        }
        const auto c1 = item.find(':');
        if (c1 == std::string_view::npos || item.substr(0, c1) != id) continue;
        if (from > poll || from < bestFrom || (from == bestFrom && out.found)) continue;
        bestFrom = from;
        out.found = true;
        const std::string_view tail = item.substr(c1 + 1);
        const auto c2 = tail.find(':');
        out.clocks.event_ms = parse_seed_ms(tail.substr(0, c2));
        out.clocks.run_complete_ms =
            c2 == std::string_view::npos ? std::nullopt : parse_seed_ms(tail.substr(c2 + 1));
    }
    return out;
}

template <typename Row>
void carry_held_rows(std::vector<Row>& fresh, const std::vector<Row>& held) {
    std::unordered_map<std::string_view, const Row*> byId;
    byId.reserve(held.size());
    for (const Row& h : held) byId.emplace(std::string_view(h.id), &h);
    if (byId.empty()) return;
    for (Row& row : fresh) {
        if (row.last_event_unix_ms) continue;
        const auto it = byId.find(std::string_view(row.id));
        if (it == byId.end()) continue;
        const Clocks heldClocks{it->second->last_event_unix_ms, it->second->last_run_complete_unix_ms};
        const Clocks carried = carry_held({row.last_event_unix_ms, row.last_run_complete_unix_ms}, &heldClocks);
        row.last_event_unix_ms = carried.event_ms;
        row.last_run_complete_unix_ms = carried.run_complete_ms;
    }
}

}
