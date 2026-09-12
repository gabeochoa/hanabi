#pragma once

// The compaction divider's words, shared by the wire parser, the loader and
// the renderer so the three cannot disagree about a number.
//
// Two states of one row. RUNNING, from the `compaction_started` ephemeral:
// "Summarizing earlier messages… 3m 01s · 9.9k tokens" -- the elapsed counted
// from the SERVER's start anchor, the tokens the summarizer's latest reading.
// Each part appears only when the wire has it: no anchor, no clock; no
// reading yet, no tokens. COMPLETE, from the durable `compacted`:
// "Earlier messages summarized", the summary itself behind the row's own
// disclosure.

#include <cstdint>
#include <string>

namespace api::compaction {

inline constexpr const char* kRunningLabel =
    "Summarizing earlier messages\xe2\x80\xa6";
inline constexpr const char* kCompleteLabel = "Earlier messages summarized";
// U+00B7 MIDDLE DOT with a space each side.
inline constexpr const char* kJoiner = " \xc2\xb7 ";

// The in-flight round as the worker last read it. started_at_unix_ms is 0 on
// a journal that predates the anchor; output_tokens is -1 until the
// summarizer's first usage report.
struct Progress {
    int64_t started_at_unix_ms = 0;
    int64_t output_tokens = -1;
};

// "5s", "3m 01s", "1h 07m" -- minutes and hours pad their trailing part to
// two digits so a clock counting up does not jitter in width.
inline std::string duration_label(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms < 1000) return std::to_string(ms) + "ms";
    const int64_t seconds = ms / 1000;
    if (seconds < 60) return std::to_string(seconds) + "s";
    const int64_t minutes = seconds / 60;
    const auto padded = [](int64_t v) {
        return v < 10 ? "0" + std::to_string(v) : std::to_string(v);
    };
    if (minutes < 60)
        return std::to_string(minutes) + "m " + padded(seconds % 60) + "s";
    return std::to_string(minutes / 60) + "h " + padded(minutes % 60) + "m";
}

// "0", "999", "9.9k", "12k", "1.5M". The unit boundary sits where a value
// ROUNDS up, so 999,500 is "1M" and not "1000k"; one decimal below 10 of a
// unit, none above, and a whole tenth drops its ".0".
inline std::string compact_tokens(int64_t value) {
    if (value <= 0) return "0";
    if (value < 1000) return std::to_string(value);
    const auto unit = [](double v, const char* suffix) -> std::string {
        if (v >= 9.95) return std::to_string(static_cast<long long>(v + 0.5)) + suffix;
        const long long tenths = static_cast<long long>(v * 10.0 + 0.5);
        if (tenths % 10 == 0) return std::to_string(tenths / 10) + suffix;
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) +
               suffix;
    };
    if (value < 999500) return unit(static_cast<double>(value) / 1e3, "k");
    if (value < 999500000) return unit(static_cast<double>(value) / 1e6, "M");
    return unit(static_cast<double>(value) / 1e9, "B");
}

// The elapsed since the anchor, clamped at zero: the stamp is the server's
// clock and `now` is this machine's, and a skew must not read as a negative
// duration.
inline int64_t elapsed_ms(const Progress& p, int64_t now_unix_ms) {
    if (p.started_at_unix_ms <= 0) return -1;
    return now_unix_ms > p.started_at_unix_ms ? now_unix_ms - p.started_at_unix_ms
                                              : 0;
}

// "3m 01s · 9.9k tokens", "3m 01s", "9.9k tokens", or "" when the wire has
// carried neither fact yet.
inline std::string detail(const Progress& p, int64_t now_unix_ms) {
    std::string out;
    const int64_t elapsed = elapsed_ms(p, now_unix_ms);
    if (elapsed >= 0) out = duration_label(elapsed);
    if (p.output_tokens >= 0) {
        if (!out.empty()) out += kJoiner;
        out += compact_tokens(p.output_tokens) + " tokens";
    }
    return out;
}

inline std::string running_label(const Progress& p, int64_t now_unix_ms) {
    const std::string d = detail(p, now_unix_ms);
    return d.empty() ? std::string(kRunningLabel)
                     : std::string(kRunningLabel) + " " + d;
}

}  // namespace api::compaction
