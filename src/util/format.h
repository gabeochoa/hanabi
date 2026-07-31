#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace fmtutil {

// Compact relative age from a unix epoch (seconds): "now", "5m", "3h", "2d"...
inline std::string relative_time(int64_t epoch) {
    if (epoch <= 0) return "";
    double secs = std::difftime(std::time(nullptr), static_cast<std::time_t>(epoch));
    if (secs < 0) secs = 0;
    long s = static_cast<long>(secs);
    if (s < 60) return "now";
    long m = s / 60;
    if (m < 60) return std::to_string(m) + "m";
    long h = m / 60;
    if (h < 24) return std::to_string(h) + "h";
    long d = h / 24;
    if (d < 7) return std::to_string(d) + "d";
    if (d < 30) return std::to_string(d / 7) + "w";
    if (d < 365) return std::to_string(d / 30) + "mo";
    return std::to_string(d / 365) + "y";
}

// Truncate to n chars with an ellipsis.
inline std::string ellipsize(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n > 1 ? n - 1 : 0) + "\xe2\x80\xa6";
}

}  // namespace fmtutil
