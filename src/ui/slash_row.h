#pragma once

#include <algorithm>
#include <string>

namespace hanabi::slash_row {

inline constexpr float kRowInset = 32.0f;
inline constexpr float kPad = 10.0f;
inline constexpr float kCommandW = 106.0f;
inline constexpr float kCommandMin = 52.0f;
inline constexpr float kStatusW = 78.0f;
inline constexpr float kStatusShortW = 34.0f;
inline constexpr float kBlurbMin = 96.0f;

struct Slots {
    float command = 0.0f;
    float blurb = 0.0f;
    float status = 0.0f;

    float total() const {
        if (command <= 0.0f && blurb <= 0.0f && status <= 0.0f) return 0.0f;
        return kRowInset + kPad + command + blurb + status;
    }
};

inline Slots budget(float row_width, bool runnable) {
    Slots out;
    float left = row_width - kRowInset - kPad;
    if (left <= 0.0f) return out;

    out.command = std::min(kCommandW, left);
    left -= out.command;

    if (!runnable) {
        if (left < kStatusW && out.command > kCommandMin) {
            const float borrow =
                std::min(out.command - kCommandMin, kStatusW - left);
            out.command -= borrow;
            left += borrow;
        }
        out.status = std::min(kStatusW, left);
        if (out.status < kStatusShortW) out.status = 0.0f;
        left -= out.status;
    }

    out.blurb = left >= kBlurbMin ? left : 0.0f;
    return out;
}

inline std::string status_label(float status_width) {
    return status_width >= kStatusW ? "Unavailable" : "n/a";
}

}  // namespace hanabi::slash_row
