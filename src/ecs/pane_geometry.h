#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "pane_layout.h"

namespace ecs::model {

inline constexpr float kDividerW = 5.0f;
inline constexpr float kMinimumPaneW = 280.0f;
inline constexpr float kAskUsablePaneW = 240.0f;

inline bool split_fits(float usable) { return usable >= kAskUsablePaneW * 2.0f; }

inline float clamped_split_left(float desired, float usable) {
    if (usable <= 0.0f) return 0.0f;
    if (!split_fits(usable)) return usable;
    const float floorW = usable >= kMinimumPaneW * 2.0f ? kMinimumPaneW : kAskUsablePaneW;
    return std::round(std::clamp(desired, floorW, usable - floorW));
}

inline float split_left_width(float ratio, float paneW) {
    const float usable = paneW - kDividerW;
    return clamped_split_left(usable * ratio, usable);
}

inline std::vector<float> pane_widths(int panes, float ratio, const std::vector<float>& shares,
                                      float paneW) {
    if (panes <= 2) {
        const float leftW = split_left_width(ratio, paneW);
        return panes == 2 ? std::vector<float>{leftW, paneW - kDividerW - leftW}
                          : std::vector<float>{paneW};
    }
    const float usable = paneW - kDividerW * static_cast<float>(panes - 1);
    return pane_widths_for(normalized_shares(shares, panes), usable, kPaneMinWidth).widths;
}

inline std::vector<float> pane_origins(const std::vector<float>& widths, float left) {
    std::vector<float> out;
    out.reserve(widths.size());
    float x = left;
    for (float w : widths) {
        out.push_back(x);
        x += w + kDividerW;
    }
    return out;
}

}  // namespace ecs::model
