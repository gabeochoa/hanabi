#pragma once

#include <cstdint>

namespace hanabi::tool_pace {

inline constexpr std::int64_t kSlowAfterMsDefault = 20000;

struct Row {
    bool background = false;
    bool running = false;
    std::int64_t created_at_unix = 0;
    std::int64_t timeout_ms = 0;
    std::int64_t check_ms = 0;
};

inline std::int64_t slow_after_ms(const Row& r) {
    std::int64_t after = r.check_ms > 0 ? r.check_ms : kSlowAfterMsDefault;
    if (r.timeout_ms > 0 && after > r.timeout_ms) after = r.timeout_ms;
    return after;
}

inline bool is_slow(const Row& r, std::int64_t now_unix) {
    if (!r.running || r.created_at_unix <= 0 || now_unix <= r.created_at_unix) return false;
    const std::int64_t elapsed_ms = (now_unix - r.created_at_unix) * 1000;
    if (r.timeout_ms > 0 && elapsed_ms >= r.timeout_ms) return true;
    return elapsed_ms >= slow_after_ms(r);
}

inline const char* mark(const Row& r, std::int64_t now_unix) {
    if (r.background) return "in the background";
    if (is_slow(r, now_unix)) return "taking a while";
    return "";
}

}
