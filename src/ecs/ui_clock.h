#pragma once

#include <chrono>
#include <optional>

#include "../native_extras.h"

namespace hanabi::ui_clock {

inline double& test_offset_seconds() {
    static double offset = 0.0;
    return offset;
}

inline double now_seconds() {
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
