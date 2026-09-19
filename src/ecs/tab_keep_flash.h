#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace hanabi::tab_keep_flash {

using Seconds = double;

inline constexpr Seconds kHold = 0.24;
inline constexpr Seconds kHoldReducedMotion = 0.6;
inline constexpr Seconds kTransition = 0.12;
inline constexpr float kLitScale = 1.6f;
inline constexpr float kRestingOpacity = 0.7f;
inline constexpr float kLitOpacity = 1.0f;

struct Mark {
    bool lit = false;
    bool animated = true;
    float progress = 0.0f;
    bool operator==(const Mark&) const = default;
};

inline float ease_in_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.5f) return 2.0f * t * t;
    const float u = 1.0f - t;
    return 1.0f - 2.0f * u * u;
}

inline float ease_in_out_inverse(float p) {
    p = std::clamp(p, 0.0f, 1.0f);
    if (p < 0.5f) return std::sqrt(p * 0.5f);
    return 1.0f - std::sqrt((1.0f - p) * 0.5f);
}

inline float scale_for(const Mark& m) {
    if (!m.animated) return 1.0f;
    return 1.0f + (kLitScale - 1.0f) * m.progress;
}

inline float opacity_for(const Mark& m) {
    return kRestingOpacity + (kLitOpacity - kRestingOpacity) * m.progress;
}

class Store {
   public:
    bool refuse_close(const std::string& tabId, bool pinned, Seconds now, bool reduceMotion) {
        if (!pinned) return false;
        sweep(now);
        Seconds start = now;
        if (const auto fading = fading_.find(tabId); fading != fading_.end()) {
            const float shown = 1.0f - ease_in_out(static_cast<float>((now - fading->second) / kTransition));
            start = now - static_cast<Seconds>(ease_in_out_inverse(shown)) * kTransition;
            fading_.erase(fading);
        }
        Entry& e = entries_[tabId];
        if (!e.lit) {
            e.lit = true;
            e.lit_at = start;
            ++generation_;
        }
        e.until = now + (reduceMotion ? kHoldReducedMotion : kHold);
        return true;
    }

    Mark mark(const std::string& tabId, Seconds now, bool reduceMotion) {
        sweep(now);
        if (const auto it = entries_.find(tabId); it != entries_.end()) {
            if (reduceMotion) return Mark{true, false, 1.0f};
            return Mark{true, true, ease_in_out(static_cast<float>((now - it->second.lit_at) / kTransition))};
        }
        const auto fading = fading_.find(tabId);
        if (fading == fading_.end() || reduceMotion) return Mark{false, !reduceMotion, 0.0f};
        const float t = static_cast<float>((now - fading->second) / kTransition);
        return Mark{false, true, 1.0f - ease_in_out(t)};
    }

    void sweep(Seconds now) {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (now >= it->second.until) {
                fading_[it->first] = it->second.until;
                it = entries_.erase(it);
                ++generation_;
            } else {
                ++it;
            }
        }
        for (auto it = fading_.begin(); it != fading_.end();) {
            if (now >= it->second + kTransition) it = fading_.erase(it);
            else ++it;
        }
    }

    void forget(const std::string& tabId) {
        entries_.erase(tabId);
        fading_.erase(tabId);
    }

    bool is_lit(const std::string& tabId) const { return entries_.count(tabId) != 0; }
    bool empty() const { return entries_.empty(); }
    bool fading(const std::string& tabId) const { return fading_.count(tabId) != 0; }
    std::uint64_t generation() const { return generation_; }

   private:
    struct Entry {
        bool lit = false;
        Seconds lit_at = 0;
        Seconds until = 0;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, Seconds> fading_;
    std::uint64_t generation_ = 0;
};

inline Store& store() {
    static Store s;
    return s;
}

}
