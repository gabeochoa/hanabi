#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hanabi::stretch {

enum class Kind : std::uint8_t {
    Step,        // a tool call that belongs to a run (consecutive Steps are one run)
    Wake,        // the delivery that woke the session
    Thinking,
    Compaction,
    Done,        // a run's "completed" outcome with nothing to say
    Boundary,    // everything the reader must see: ends a stretch
};

inline bool routine(Kind k) { return k != Kind::Boundary; }

inline constexpr int kFloor = 2;

struct Block {
    std::size_t lo = 0, hi = 0;  // [lo, hi)
    int items = 0;
    int runs = 0;
    [[nodiscard]] bool empty() const { return hi <= lo; }
};

template <class KindAt>
Block block_at(std::size_t lo, std::size_t n, KindAt&& kind_at) {
    Block b;
    b.lo = lo;
    if (lo >= n || !routine(kind_at(lo))) {
        b.hi = lo;
        return b;
    }
    std::size_t e = lo;
    bool inRun = false;
    for (; e < n && routine(kind_at(e)); ++e) {
        const Kind k = kind_at(e);
        if (k == Kind::Step) {
            if (!inRun) {
                ++b.runs;
                ++b.items;
                inRun = true;
            }
        } else {
            inRun = false;
            ++b.items;
        }
    }
    b.hi = e;
    return b;
}

inline bool folds(const Block& b, std::size_t n, bool live_tail) {
    if (b.empty() || b.runs < 1 || b.items < kFloor) return false;
    if (live_tail && b.hi == n) return false;
    return true;
}

struct Facts {
    int runs = 0;
    int steps = 0;
    int wakes = 0;
    int errors = 0;
    std::int64_t active_ms = 0;   // the tools' own durations, summed
    std::int64_t span_ms = 0;     // newest stamp minus oldest, wall clock
    bool streaming = false;
};

inline std::string duration_label(std::int64_t ms) {
    if (ms <= 0) return "0s";
    const std::int64_t s = ms / 1000;
    if (s < 60) return std::to_string(s) + "s";
    const std::int64_t m = s / 60;
    if (m < 60) {
        const std::int64_t rs = s % 60;
        return rs ? std::to_string(m) + "m " + std::to_string(rs) + "s" : std::to_string(m) + "m";
    }
    const std::int64_t h = m / 60;
    const std::int64_t rm = m % 60;
    if (h < 48) return rm ? std::to_string(h) + "h " + std::to_string(rm) + "m" : std::to_string(h) + "h";
    return std::to_string(h / 24) + "d " + std::to_string(h % 24) + "h";
}

inline std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

inline std::string summary(const Facts& f) {
    std::string text = "Worked";
    const bool haveActive = f.active_ms > 0;
    if (haveActive) text += " for " + duration_label(f.active_ms);
    if (f.span_ms > 0) text += std::string(haveActive ? " across " : " for ") + duration_label(f.span_ms);
    std::string counts;
    if (f.wakes > 0) counts += count(f.wakes, "wake", "wakes") + " \xc2\xb7 ";
    counts += count(f.runs, "run", "runs") + " \xc2\xb7 " + count(f.steps, "step", "steps");
    if (f.errors > 0) counts += " \xc2\xb7 " + count(f.errors, "error", "errors");
    return text + " \xe2\x80\x94 " + counts;
}

inline std::string wake_header(const Facts& f) {
    if (f.steps > 0 || f.wakes == 0) return {};
    std::string text = "Woke up " + count(f.wakes, "time", "times");
    if (f.errors > 0) text += " \xc2\xb7 " + count(f.errors, "error", "errors");
    return text;
}

inline std::string line(const Facts& f) {
    const std::string wakes = wake_header(f);
    return wakes.empty() ? summary(f) : wakes;
}

}  // namespace hanabi::stretch
