#pragma once

#include <string>
#include <string_view>

namespace hanabi::run_outcome {

inline constexpr std::string_view kInterrupted = "interrupted";
inline constexpr std::string_view kStoppedByYou = "Stopped by you";

inline std::string label(std::string_view outcome) {
    if (outcome == kInterrupted) return std::string(kStoppedByYou);
    if (outcome == "handed_off") return "Handed off";
    return std::string(outcome);
}

inline std::string label(std::string_view outcome, std::string_view note) {
    std::string out = label(outcome);
    if (!note.empty()) out += " \xc2\xb7 " + std::string(note);
    return out;
}

}
