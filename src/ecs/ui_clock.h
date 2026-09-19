#pragma once

#include <charconv>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>
#include <system_error>

#include "../native_extras.h"

namespace hanabi::ui_clock {

inline double& test_offset_seconds() {
    static double offset = 0.0;
    return offset;
}

inline constexpr double kMaxTestOffsetSeconds = 1.0e6;

inline bool advance_test_offset(std::string_view text) {
    if (text.empty()) return false;
    double value = 0.0;
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc() || result.ptr != end) return false;
    if (!std::isfinite(value) || value < 0.0) return false;
    if (test_offset_seconds() + value > kMaxTestOffsetSeconds) return false;
    test_offset_seconds() += value;
    return true;
}

inline bool& headless_run() {
    static bool v = false;
    return v;
}

inline double now_seconds() {
    if (headless_run()) return test_offset_seconds();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() +
           test_offset_seconds();
}

inline std::optional<bool>& reduce_motion_override() {
    static std::optional<bool> value;
    return value;
}

inline bool reduce_motion() {
    if (reduce_motion_override().has_value()) return *reduce_motion_override();
    return hanabi::os_reduce_motion();
}

}
