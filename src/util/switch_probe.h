#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hanabi::switch_probe {

struct Open {
    std::string id;
    double ms = 0.0;
    int frames = 0;
    int rows = 0;
    bool from_cache = false;
    double fresh_ms = -1.0;
    int fetched_rows = 0;
    bool delta = false;
    std::int64_t begun_us = 0;
};

struct Summary {
    int count = 0;
    double min_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;
    double p50_frames = 0.0;
    int cached = 0;
    int fresh_count = 0;
    double fresh_p50_ms = 0.0;
    double fresh_p95_ms = 0.0;
    long fetched_rows = 0;
    int full = 0;
    int since = 0;
};

inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * frac;
}

inline Summary summarize(const std::vector<Open>& opens) {
    Summary s;
    s.count = static_cast<int>(opens.size());
    if (opens.empty()) return s;
    std::vector<double> ms, frames, fresh;
    ms.reserve(opens.size());
    frames.reserve(opens.size());
    for (const Open& o : opens) {
        ms.push_back(o.ms);
        frames.push_back(static_cast<double>(o.frames));
        if (o.from_cache) ++s.cached;
        if (o.fresh_ms >= 0.0) {
            fresh.push_back(o.fresh_ms);
            ++s.fresh_count;
            s.fetched_rows += o.fetched_rows;
            if (o.delta) ++s.since;
            else ++s.full;
        }
    }
    s.min_ms = *std::min_element(ms.begin(), ms.end());
    s.max_ms = *std::max_element(ms.begin(), ms.end());
    s.p50_ms = percentile(ms, 0.5);
    s.p95_ms = percentile(ms, 0.95);
    s.p50_frames = percentile(frames, 0.5);
    s.fresh_p50_ms = percentile(fresh, 0.5);
    s.fresh_p95_ms = percentile(fresh, 0.95);
    return s;
}

struct Pending {
    std::string id;
    std::int64_t begun_us = 0;
    unsigned long begun_frame = 0;
    bool armed = false;
};

class Probe {
   public:
    void begin(const std::string& id, std::int64_t now_us, unsigned long frame) {
        pending_.id = id;
        pending_.begun_us = now_us;
        pending_.begun_frame = frame;
        pending_.armed = !id.empty();
    }
    bool end(const std::string& id, std::int64_t now_us, unsigned long frame, int rows,
             bool from_cache) {
        if (!pending_.armed || pending_.id != id || rows <= 0) return false;
        Open o;
        o.id = id;
        o.ms = static_cast<double>(now_us - pending_.begun_us) / 1000.0;
        o.frames = static_cast<int>(frame - pending_.begun_frame);
        o.rows = rows;
        o.from_cache = from_cache;
        o.begun_us = pending_.begun_us;
        opens_.push_back(std::move(o));
        pending_.armed = false;
        return true;
    }
    const Open* fresh(const std::string& id, std::int64_t now_us, int fetchedRows, bool delta) {
        for (auto it = opens_.rbegin(); it != opens_.rend(); ++it) {
            if (it->id != id) continue;
            if (it->fresh_ms >= 0.0) return nullptr;
            it->fresh_ms = static_cast<double>(now_us - it->begun_us) / 1000.0;
            it->fetched_rows = fetchedRows;
            it->delta = delta;
            return &*it;
        }
        if (pending_.armed && pending_.id == id) {
            Open o;
            o.id = id;
            o.ms = static_cast<double>(now_us - pending_.begun_us) / 1000.0;
            o.frames = -1;
            o.fresh_ms = o.ms;
            o.fetched_rows = fetchedRows;
            o.delta = delta;
            o.begun_us = pending_.begun_us;
            opens_.push_back(std::move(o));
            return &opens_.back();
        }
        return nullptr;
    }
    [[nodiscard]] bool armed() const { return pending_.armed; }
    [[nodiscard]] const std::vector<Open>& opens() const { return opens_; }
    [[nodiscard]] Summary summary() const { return summarize(opens_); }

   private:
    Pending pending_;
    std::vector<Open> opens_;
};

inline bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SWITCH_PROBE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    return on;
}

inline Probe& probe() {
    static Probe p;
    return p;
}

inline void print_open(const Open& o) {
    std::printf("[switch] open %s  %.2f ms  %d frames  rows %d%s\n", o.id.c_str(), o.ms, o.frames,
                o.rows, o.from_cache ? "  (cache)" : "");
}

inline void print_fresh(const Open& o) {
    std::printf("[switch] fresh %s  %.2f ms  rows %d  %s\n", o.id.c_str(), o.fresh_ms, o.fetched_rows,
                o.delta ? "since" : "full");
}

inline void print_summary(const Summary& s) {
    std::printf("[switch] summary opens=%d min=%.2f p50=%.2f p95=%.2f max=%.2f ms  p50=%.1f frames  "
                "cached=%d\n",
                s.count, s.min_ms, s.p50_ms, s.p95_ms, s.max_ms, s.p50_frames, s.cached);
    std::printf("[switch] fresh landed=%d p50=%.2f p95=%.2f ms  rows_fetched=%ld  full=%d since=%d\n",
                s.fresh_count, s.fresh_p50_ms, s.fresh_p95_ms, s.fetched_rows, s.full, s.since);
    std::fflush(stdout);
}

}
