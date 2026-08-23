#pragma once

// Case-insensitive substring scanning, with no dependency on graphics or the
// UI layer — so the same function the renderer highlights from can be unit
// tested in a headless build that has no backend at all.

#include <cstddef>
#include <string>
#include <vector>

namespace textscan {

inline char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Byte offsets of every occurrence of `needle` in `hay`, case-insensitively.
// Non-overlapping: the scan resumes past each hit, so "aa" occurs twice in
// "aaaa", not three times.
inline std::vector<size_t> occurrences(const std::string& hay,
                                       const std::string& needle) {
    std::vector<size_t> out;
    if (needle.empty() || hay.size() < needle.size()) return out;
    for (size_t i = 0; i + needle.size() <= hay.size();) {
        bool hit = true;
        for (size_t j = 0; j < needle.size(); ++j)
            if (lower_ascii(hay[i + j]) != lower_ascii(needle[j])) {
                hit = false;
                break;
            }
        if (hit) {
            out.push_back(i);
            i += needle.size();
        } else {
            ++i;
        }
    }
    return out;
}

}  // namespace textscan
