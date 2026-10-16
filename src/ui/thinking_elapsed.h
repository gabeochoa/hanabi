#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hanabi::thinking_elapsed {

inline std::string format(std::int64_t ms) {
    if (ms < 1000) return std::string();
    const std::int64_t secs = ms / 1000;
    if (secs < 60) return std::to_string(secs) + "s";
    const std::int64_t mins = secs / 60;
    const std::int64_t rest = secs % 60;
    if (mins < 60) {
        return std::to_string(mins) + "m " + (rest < 10 ? "0" : "") + std::to_string(rest) + "s";
    }
    const std::int64_t hours = mins / 60;
    const std::int64_t mrest = mins % 60;
    return std::to_string(hours) + "h " + (mrest < 10 ? "0" : "") + std::to_string(mrest) + "m";
}

inline std::size_t word_count(std::string_view text) {
    std::size_t words = 0;
    bool in_word = false;
    for (char c : text) {
        const bool space = (c == ' ' || c == '\n' || c == '\t');
        if (!space && !in_word) ++words;
        in_word = !space;
    }
    return words;
}

inline std::string summary(std::string_view text, std::int64_t ms) {
    const std::string took = format(ms);
    std::string out = took.empty() ? "Thought for a moment" : "Thought for " + took;
    const std::size_t words = word_count(text);
    if (words == 0) return out;
    out += "  \xc2\xb7  " + std::to_string(words) + (words == 1 ? " word" : " words");
    return out;
}

}
