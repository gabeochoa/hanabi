#pragma once

// The cells a `std/Table` element carried, drawn as the table and not only as
// the server's one-line summary (Knots kt-nooi / kt-h6g6: "none of these
// tables render" -- the reference drew "Table: 5 rows (Activity, Measured)"
// and none of the values). Pure.
//
// Read from the wire tree's props (`tree.p.cols`, `tree.p.rows`), bounded the
// way the reference bounds them: at most kRowCap rows and kColCap columns,
// kCellCap bytes a cell; anything past a cap is dropped and an ellipsis line
// says so. The emit's own `maxRows` holds rows back and says how many. Cells
// are scalars -- strings, numbers, booleans, null; anything else is not a
// table this client draws, and the row falls back to the projection.

#include <optional>
#include <string>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::elements {

inline constexpr const char* kTableElement = "std/Table";
inline constexpr std::size_t kRowCap = 200;
inline constexpr std::size_t kColCap = 20;
inline constexpr std::size_t kCellCap = 2000;

struct Table {
    std::vector<std::string> cols;
    std::vector<std::vector<std::string>> rows;
    bool truncated = false;
    std::size_t heldBack = 0;
};

namespace detail {
inline bool cell_text(const nlohmann::json& v, std::string* out, bool* truncated) {
    if (v.is_string()) *out = v.get<std::string>();
    else if (v.is_boolean()) *out = v.get<bool>() ? "true" : "false";
    else if (v.is_number_integer()) *out = std::to_string(v.get<long long>());
    else if (v.is_number_unsigned()) *out = std::to_string(v.get<unsigned long long>());
    else if (v.is_number_float()) {
        *out = v.dump();
    } else if (v.is_null()) out->clear();
    else return false;
    for (char& c : *out)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (out->size() > kCellCap) {
        std::size_t n = kCellCap;
        while (n > 0 && (static_cast<unsigned char>((*out)[n]) & 0xC0) == 0x80) --n;
        out->resize(n);
        *truncated = true;
    }
    return true;
}
}  // namespace detail

// The table a std/Table's props carry, or nothing (the caller draws the
// projection).
inline std::optional<Table> table_from_props(const nlohmann::json& p) {
    if (!p.is_object() || !p.contains("rows") || !p["rows"].is_array()) return std::nullopt;
    const auto& rawRows = p["rows"];
    Table t;
    std::size_t printable = rawRows.size();
    if (p.contains("maxRows") && p["maxRows"].is_number_integer()) {
        const long long cap = p["maxRows"].get<long long>();
        if (cap > 0 && rawRows.size() > static_cast<std::size_t>(cap)) {
            t.heldBack = rawRows.size() - static_cast<std::size_t>(cap);
            printable = static_cast<std::size_t>(cap);
        }
    }
    if (p.contains("cols") && p["cols"].is_array()) {
        const auto& rc = p["cols"];
        if (rc.size() > kColCap) t.truncated = true;
        for (std::size_t i = 0; i < rc.size() && i < kColCap; ++i) {
            std::string c;
            if (!detail::cell_text(rc[i], &c, &t.truncated)) return std::nullopt;
            t.cols.push_back(std::move(c));
        }
    }
    if (printable > kRowCap) t.truncated = true;
    for (std::size_t r = 0; r < printable && r < kRowCap; ++r) {
        const auto& raw = rawRows[r];
        if (!raw.is_array()) return std::nullopt;
        if (raw.size() > kColCap) t.truncated = true;
        std::vector<std::string> row;
        for (std::size_t i = 0; i < raw.size() && i < kColCap; ++i) {
            std::string c;
            if (!detail::cell_text(raw[i], &c, &t.truncated)) return std::nullopt;
            row.push_back(std::move(c));
        }
        t.rows.push_back(std::move(row));
    }
    if (t.cols.empty() && t.rows.empty()) return std::nullopt;
    return t;
}

inline std::string join_cells(const std::vector<std::string>& cells) {
    std::string out;
    for (std::size_t i = 0; i < cells.size(); ++i) out += (i ? " | " : "") + cells[i];
    return out;
}

// Every header and value as text, one row a line (the reference's plainText):
// what the row draws, and what search, copy and export read.
inline std::string table_text(const Table& t) {
    std::string out;
    if (!t.cols.empty()) out = join_cells(t.cols);
    for (const auto& r : t.rows) out += (out.empty() ? "" : "\n") + join_cells(r);
    if (t.heldBack > 0) out += "\n(+" + std::to_string(t.heldBack) + " more rows)";
    else if (t.truncated) out += "\n\xe2\x80\xa6";
    return out;
}

// The text for a std/Table's props, or "" when they carry no table.
inline std::string table_text_from_props(const std::string& element, const nlohmann::json& p) {
    if (element != kTableElement) return {};
    const auto t = table_from_props(p);
    return t ? table_text(*t) : std::string();
}

}  // namespace api::elements
