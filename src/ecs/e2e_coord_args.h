#pragma once

// A coordinate command's x and y, checked before the library parses them
// (afterhours_gaps.md #611). The script parser hands a coordinate command two
// args whether or not the line had them -- empty strings for a bare
// `mouse_down` -- so the library handler's `has_args(2)` passes and
// `coord_arg` throws std::invalid_argument out of stof: the whole run aborts
// with no line number. This names the line instead.

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::e2e {

inline constexpr std::string_view kCoordCommands[] = {
    "click", "double_click", "triple_click", "mouse_move", "mouse_down", "middle_click",
    "middle_down"};

inline bool is_coord_command(std::string_view name) {
    for (std::string_view c : kCoordCommands)
        if (c == name) return true;
    return false;
}

// A number, optionally ending in '%' (a screen percentage), and nothing else.
inline bool is_coord(const std::string& s) {
    if (s.empty()) return false;
    const std::string body = s.back() == '%' ? s.substr(0, s.size() - 1) : s;
    if (body.empty()) return false;
    char* end = nullptr;
    std::strtof(body.c_str(), &end);
    return end != nullptr && end != body.c_str() && *end == '\0';
}

inline bool coord_args_ok(const std::vector<std::string>& args) {
    return args.size() >= 2 && is_coord(args[0]) && is_coord(args[1]);
}

}  // namespace hanabi::e2e
