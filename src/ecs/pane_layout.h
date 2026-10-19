#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ecs::model {

inline constexpr float kShareEpsilon = 1e-4f;
inline constexpr float kPaneMinWidth = 480.0f;

inline std::vector<float> even_shares(int panes) {
    if (panes <= 0) return {};
    return std::vector<float>(static_cast<std::size_t>(panes), 1.0f / static_cast<float>(panes));
}

inline std::vector<float> normalized_shares(const std::vector<float>& shares, int panes) {
    if (panes <= 0) return {};
    if (shares.size() != static_cast<std::size_t>(panes)) return even_shares(panes);
    float total = 0.0f;
    for (float s : shares) {
        if (!(s > 0.0f) || !std::isfinite(s)) return even_shares(panes);
        total += s;
    }
    std::vector<float> out;
    out.reserve(shares.size());
    for (float s : shares) out.push_back(s / total);
    return out;
}

inline std::vector<float> shares_after_insert(const std::vector<float>& shares, int at) {
    (void)at;
    return even_shares(static_cast<int>(shares.size()) + 1);
}

inline std::vector<float> shares_after_remove(const std::vector<float>& shares, int at) {
    if (shares.size() <= 1 || at < 0 || at >= static_cast<int>(shares.size())) return even_shares(1);
    std::vector<float> out = shares;
    out.erase(out.begin() + at);
    return normalized_shares(out, static_cast<int>(out.size()));
}

inline std::vector<float> shares_after_drag(const std::vector<float>& shares, int divider,
                                            float deltaPx, float usablePx, float minPx) {
    if (divider < 0 || divider + 1 >= static_cast<int>(shares.size()) || !(usablePx > 0.0f))
        return shares;
    std::vector<float> out = shares;
    const std::size_t left = static_cast<std::size_t>(divider);
    const std::size_t right = left + 1;
    const float pair = out[left] + out[right];
    const float floorShare = std::min(minPx / usablePx, pair / 2.0f);
    const float wanted = out[left] + deltaPx / usablePx;
    out[left] = std::clamp(wanted, floorShare, pair - floorShare);
    out[right] = pair - out[left];
    return out;
}

struct PaneWidths {
    std::vector<float> widths;
    float total = 0.0f;
    bool overflows = false;
};

inline PaneWidths pane_widths_for(const std::vector<float>& shares, float usablePx, float minPx) {
    PaneWidths out;
    const int panes = static_cast<int>(shares.size());
    const std::vector<float> s = normalized_shares(shares, panes);
    for (float share : s) {
        const float w = std::max(share * usablePx, minPx);
        out.widths.push_back(w);
        out.total += w;
    }
    out.overflows = out.total > usablePx + kShareEpsilon;
    return out;
}

inline float window_extent_needed(int panes, float dividerPx, float minPx, float chromePx) {
    if (panes <= 0) return chromePx;
    return chromePx + minPx * static_cast<float>(panes) + dividerPx * static_cast<float>(panes - 1);
}

}  // namespace ecs::model
