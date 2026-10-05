#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "types.h"

namespace api::elements {

inline constexpr std::string_view kRowIdPrefix = "element:";
inline constexpr std::size_t kInstanceMaxBytes = 120;
inline constexpr std::size_t kProjectionMaxBytes = 16 * 1024;

inline std::string row_id(std::string_view instance) {
    return std::string(kRowIdPrefix) + std::string(instance);
}

inline bool is_element_row(const Message& m) {
    return m.kind == EventKind::Element;
}

inline bool facts_are_readable(const ElementFacts& f) {
    return !f.instance.empty() && f.instance.size() <= kInstanceMaxBytes &&
           f.revision >= 1 && !f.placement.empty() &&
           f.projection.size() <= kProjectionMaxBytes;
}

inline std::string heading(const ElementFacts& f) {
    return f.title.empty() ? f.element : f.title;
}

inline std::string provenance(const ElementFacts& f) {
    std::vector<std::string> parts;
    if (f.placement == "artifact") {
        parts.push_back(f.artifact_id.empty() ? std::string("artifact (no handle)")
                                              : "artifact " + f.artifact_id);
    } else if (!f.placement.empty() && f.placement != "inline") {
        parts.push_back(f.placement);
    }
    if (!f.title.empty() && !f.element.empty()) parts.push_back(f.element);
    if (f.revision > 1) parts.push_back("rev " + std::to_string(f.revision));
    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty()) out += " \xc2\xb7 ";
        out += p;
    }
    return out;
}

// What the row shows: a table's cells when it carried them (kt-nooi: the
// projection of a table is a one-line summary, not its rows), else the
// projection, else the heading.
inline std::string row_text(const ElementFacts& f) {
    if (!f.table_text.empty()) return f.table_text;
    return f.projection.empty() ? heading(f) : f.projection;
}

inline std::size_t longest_backtick_run(std::string_view text) {
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char c : text) {
        run = (c == '`') ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

inline std::string fence_enclosing(std::string_view text) {
    return std::string(std::max<std::size_t>(3, longest_backtick_run(text) + 1), '`');
}

inline std::string export_block(const Message& m) {
    const std::string head = heading(m.element);
    const std::string prov = provenance(m.element);
    const std::string body = m.text.empty() ? head : m.text;
    const std::string fence = fence_enclosing(body);
    std::string out = "### **Element: " + head + "**";
    if (!prov.empty()) out += " (" + prov + ")";
    out += "\n\n" + fence + "text\n" + body + "\n" + fence + "\n\n";
    return out;
}

inline void install_facts(Message& row, const ElementFacts& facts) {
    const std::uint64_t anchor = row.element.anchor_seq;
    const std::string held_title = row.element.title;
    row.element = facts;
    row.element.anchor_seq = anchor;
    if (row.element.title.empty()) row.element.title = held_title;
    row.subtitle = heading(row.element);
    row.text = row_text(row.element);
}

inline Message make_row(const ElementFacts& facts, std::uint64_t anchor_seq,
                        std::int64_t created_at) {
    Message row;
    row.id = row_id(facts.instance);
    row.role = Role::System;
    row.kind = EventKind::Element;
    row.created_at = created_at;
    install_facts(row, facts);
    row.element.anchor_seq = anchor_seq;
    return row;
}

inline std::uint64_t order_seq(const Message& m) {
    if (is_element_row(m)) return m.element.anchor_seq;
    if (m.id.empty() || m.sync != SyncState::None) return 0;
    std::uint64_t seq = 0;
    for (const char c : m.id) {
        if (c < '0' || c > '9') return 0;
        seq = seq * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return seq;
}

inline std::size_t insertion_index(const std::vector<Message>& rows,
                                   std::uint64_t seq) {
    if (seq == 0) return rows.size();
    bool saw_ordered = false;
    for (std::size_t i = rows.size(); i > 0; --i) {
        const std::uint64_t at = order_seq(rows[i - 1]);
        if (at == 0) continue;
        saw_ordered = true;
        if (at <= seq) return i;
    }
    return saw_ordered ? 0 : rows.size();
}

inline std::size_t index_of_instance(const std::vector<Message>& rows,
                                     std::string_view instance) {
    const std::string id = row_id(instance);
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == id) return i;
    return rows.size();
}

struct FoldOutcome {
    enum class Kind { Inserted, Replaced, Moved, ReplacedAndMoved, Unchanged };
    Kind kind = Kind::Unchanged;
    std::size_t index = 0;
    std::size_t from = 0;

    [[nodiscard]] bool changed() const { return kind != Kind::Unchanged; }
    [[nodiscard]] bool moved() const {
        return kind == Kind::Moved || kind == Kind::ReplacedAndMoved;
    }
};

inline FoldOutcome fold_element(std::vector<Message>& rows,
                                const ElementFacts& facts, std::uint64_t seq,
                                std::int64_t created_at) {
    FoldOutcome out;
    const std::size_t held_at = index_of_instance(rows, facts.instance);
    if (held_at == rows.size()) {
        out.kind = FoldOutcome::Kind::Inserted;
        out.index = insertion_index(rows, seq);
        rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(out.index),
                    make_row(facts, seq, created_at));
        return out;
    }

    Message& held = rows[held_at];
    const bool newer = facts.revision > held.element.revision;
    const bool lower_anchor =
        seq != 0 && (held.element.anchor_seq == 0 || seq < held.element.anchor_seq);
    if (!newer && !lower_anchor) {
        out.index = held_at;
        return out;
    }
    if (newer) {
        install_facts(held, facts);
        if (created_at > 0) held.created_at = created_at;
    }
    if (!lower_anchor) {
        out.kind = FoldOutcome::Kind::Replaced;
        out.index = held_at;
        return out;
    }
    held.element.anchor_seq = seq;
    Message moving = std::move(held);
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(held_at));
    out.from = held_at;
    out.index = insertion_index(rows, seq);
    rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(out.index), std::move(moving));
    out.kind = newer ? FoldOutcome::Kind::ReplacedAndMoved : FoldOutcome::Kind::Moved;
    return out;
}

inline FoldOutcome fold_element_row(std::vector<Message>& rows, const Message& fresh) {
    return fold_element(rows, fresh.element, fresh.element.anchor_seq, fresh.created_at);
}

}
