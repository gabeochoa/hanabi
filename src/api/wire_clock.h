#pragma once

#include <cstdint>
#include <optional>
#include <string>
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

}
