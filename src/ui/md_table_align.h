#pragma once

// A markdown table's column alignment, read off its separator row (Knots
// kt-gkph: "honor Markdown table column alignment"). Pure.
//
//   |:---|  left     |:---:|  center     |---:|  right     |---|  left (the default)
//
// One entry per cell of the separator row; a column past the end is Left.

#include <string>
#include <vector>

namespace hanabi::md_table {

enum class Align { Left, Center, Right };

inline std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

inline std::vector<Align> alignments(const std::string& separator) {
    std::string line = trim(separator);
    if (!line.empty() && line.front() == '|') line.erase(0, 1);
    if (!line.empty() && line.back() == '|') line.pop_back();
    std::vector<Align> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t bar = line.find('|', start);
        const std::string cell = trim(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        const bool l = !cell.empty() && cell.front() == ':';
        const bool r = cell.size() > 1 && cell.back() == ':';
        out.push_back(l && r ? Align::Center : (r ? Align::Right : Align::Left));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}

inline Align at(const std::vector<Align>& a, std::size_t col) {
    return col < a.size() ? a[col] : Align::Left;
}

}  // namespace hanabi::md_table
