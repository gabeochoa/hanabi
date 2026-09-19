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
        float from = 0.0f;
        if (const auto fading = fading_.find(tabId); fading != fading_.end()) {
            from = fading->second.at(now);
            fading_.erase(fading);
        }
        Entry& e = entries_[tabId];
        if (!e.lit) {
            e.lit = true;
            e.rise = Segment{from, 1.0f, now};
            ++generation_;
        }
        e.until = now + (reduceMotion ? kHoldReducedMotion : kHold);
        return true;
    }

    Mark mark(const std::string& tabId, Seconds now, bool reduceMotion) {
        sweep(now);
        if (const auto it = entries_.find(tabId); it != entries_.end()) {
            if (reduceMotion) return Mark{true, false, 1.0f};
            return Mark{true, true, it->second.rise.at(now)};
        }
        const auto fading = fading_.find(tabId);
        if (fading == fading_.end()) return Mark{false, !reduceMotion, 0.0f};
        if (reduceMotion) {
            fading_.erase(fading);
            return Mark{false, false, 0.0f};
        }
        return Mark{false, true, fading->second.at(now)};
    }

    void sweep(Seconds now) {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (now >= it->second.until) {
                fading_[it->first] = Segment{it->second.rise.at(it->second.until), 0.0f, it->second.until};
                it = entries_.erase(it);
                ++generation_;
            } else {
                ++it;
            }
        }
        for (auto it = fading_.begin(); it != fading_.end();) {
            if (now >= it->second.started + kTransition) it = fading_.erase(it);
            else ++it;
        }
    }

    void forget(const std::string& tabId) {
        entries_.erase(tabId);
        fading_.erase(tabId);
    }

    bool is_lit(const std::string& tabId) const { return entries_.count(tabId) != 0; }
    bool empty() const { return entries_.empty() && fading_.empty(); }
    bool fading(const std::string& tabId) const { return fading_.count(tabId) != 0; }
    std::uint64_t generation() const { return generation_; }

   private:
    struct Segment {
        float from = 0.0f;
        float to = 1.0f;
        Seconds started = 0;
        float at(Seconds now) const {
            return from + (to - from) * ease_in_out(static_cast<float>((now - started) / kTransition));
        }
    };
    struct Entry {
        bool lit = false;
        Segment rise;
        Seconds until = 0;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, Segment> fading_;
    std::uint64_t generation_ = 0;
};

inline Store& store() {
    static Store s;
    return s;
}

}
