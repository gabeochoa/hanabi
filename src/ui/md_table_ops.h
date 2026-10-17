#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::md_table_ops {

using Rows = std::vector<std::vector<std::string>>;

inline constexpr int kRowCap = 50;

struct Cap {
    int shown = 0;
    int hidden = 0;
};

inline Cap cap_rows(int total, bool expanded, int cap = kRowCap) {
    Cap c;
    if (total <= 0) return c;
    const int body = total - 1;
    if (expanded || body <= cap) {
        c.shown = total;
        return c;
    }
    c.shown = 1 + cap;
    c.hidden = body - cap;
    return c;
}

inline std::string more_rows_label(int hidden) {
    return std::to_string(hidden) + (hidden == 1 ? " more row" : " more rows");
}

inline std::string escape_pipes(std::string_view cell) {
    std::string out;
    out.reserve(cell.size());
    for (char c : cell) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

inline std::string to_markdown(const Rows& rows) {
    if (rows.empty()) return {};
    std::size_t cols = 0;
    for (const auto& r : rows) cols = r.size() > cols ? r.size() : cols;
    if (cols == 0) return {};
    std::string out;
    const auto line = [&](const std::vector<std::string>& r) {
        out += "|";
        for (std::size_t i = 0; i < cols; ++i) {
            out += " ";
            out += i < r.size() ? escape_pipes(r[i]) : std::string();
            out += " |";
        }
        out += "\n";
    };
    line(rows[0]);
    out += "|";
    for (std::size_t i = 0; i < cols; ++i) out += " --- |";
    out += "\n";
    for (std::size_t ri = 1; ri < rows.size(); ++ri) line(rows[ri]);
    return out;
}

inline std::string to_tsv(const Rows& rows) {
    std::string out;
    for (const auto& r : rows) {
        for (std::size_t i = 0; i < r.size(); ++i) {
            if (i) out += '\t';
            for (char c : r[i]) out += (c == '\t' || c == '\n') ? ' ' : c;
        }
        out += '\n';
    }
    return out;
}

inline float natural_width(const Rows& rows, float charPx, float cellPad) {
    std::size_t cols = 0;
    for (const auto& r : rows) cols = r.size() > cols ? r.size() : cols;
    float total = 0.0f;
    for (std::size_t c = 0; c < cols; ++c) {
        std::size_t longest = 0;
        for (const auto& r : rows)
            if (c < r.size() && r[c].size() > longest) longest = r[c].size();
        total += static_cast<float>(longest) * charPx + cellPad;
    }
    return total;
}

}
