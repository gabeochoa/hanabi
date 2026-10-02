#pragma once

// The files a conversation changed (the reference's 0.8.9 "files changed" chip
// and panel), folded client-side from the transcript's APPLIED edit and write
// calls -- the way the web does it, so no two surfaces can disagree about
// which files a session touched.
//
// Rules, each the reference's:
//   * only a call that APPLIED (completed) counts; a failed edit changed
//     nothing;
//   * an edit carries old_string/new_string (replace_all optional), a write
//     carries the whole file in `content`; the path is file_path / path /
//     notebook_path;
//   * one file per node:path, most recently changed first, its edits oldest
//     first;
//   * the whole file is known only from the last write, with every later
//     edit placed exactly (one hit, or replace_all with any) -- otherwise it
//     is not known and the panel says so rather than guessing.
//
// Pure: no ECS, no UI. The line diff keeps context lines and falls back to
// remove-all/add-all past a cell budget, so a pathological pair cannot stall
// a frame.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>



#include "tool_kinds.h"

namespace api::changes {

enum class Side { Context, Added, Removed };

struct Line {
    Side side = Side::Context;
    std::string text;
};

inline std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) {
            out.emplace_back(text.substr(at));
            break;
        }
        out.emplace_back(text.substr(at, nl - at));
        at = nl + 1;
        if (at == text.size()) break;  // a trailing newline ends the last line
    }
    return out;
}

inline constexpr std::size_t kCellBudget = 1'000'000;

inline std::vector<Line> diff_lines(std::string_view oldText, std::string_view newText) {
    const std::vector<std::string> a = split_lines(oldText);
    const std::vector<std::string> b = split_lines(newText);
    std::size_t prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
        ++suffix;
    std::vector<Line> out;
    for (std::size_t i = 0; i < prefix; ++i) out.push_back({Side::Context, a[i]});
    const std::size_t na = a.size() - prefix - suffix;
    const std::size_t nb = b.size() - prefix - suffix;
    const auto A = [&](std::size_t i) -> const std::string& { return a[prefix + i]; };
    const auto B = [&](std::size_t j) -> const std::string& { return b[prefix + j]; };
    if (na == 0) {
        for (std::size_t j = 0; j < nb; ++j) out.push_back({Side::Added, B(j)});
    } else if (nb == 0) {
        for (std::size_t i = 0; i < na; ++i) out.push_back({Side::Removed, A(i)});
    } else if (na * nb > kCellBudget) {
        for (std::size_t i = 0; i < na; ++i) out.push_back({Side::Removed, A(i)});
        for (std::size_t j = 0; j < nb; ++j) out.push_back({Side::Added, B(j)});
    } else {
        const std::size_t w = nb + 1;
        std::vector<std::int32_t> t((na + 1) * w, 0);
        for (std::size_t i = na; i-- > 0;)
            for (std::size_t j = nb; j-- > 0;)
                t[i * w + j] = A(i) == B(j) ? t[(i + 1) * w + j + 1] + 1
                                            : std::max(t[(i + 1) * w + j], t[i * w + j + 1]);
        std::size_t i = 0, j = 0;
        while (i < na && j < nb) {
            if (A(i) == B(j)) {
                out.push_back({Side::Context, A(i)});
                ++i;
                ++j;
            } else if (t[(i + 1) * w + j] >= t[i * w + j + 1]) {
                out.push_back({Side::Removed, A(i++)});
            } else {
                out.push_back({Side::Added, B(j++)});
            }
        }
        while (i < na) out.push_back({Side::Removed, A(i++)});
        while (j < nb) out.push_back({Side::Added, B(j++)});
    }
    for (std::size_t k = a.size() - suffix; k < a.size(); ++k)
        out.push_back({Side::Context, a[k]});
    return out;
}

struct Edit {
    std::string oldText;
    std::string newText;
    bool replaceAll = false;
    bool writesWhole = false;  // a write: the whole file, no old side
    int additions = 0;
    int deletions = 0;
};

inline Edit make_edit(std::string oldText, std::string newText, bool replaceAll,
                      bool writesWhole) {
    Edit e{std::move(oldText), std::move(newText), replaceAll, writesWhole, 0, 0};
    for (const Line& l : diff_lines(e.oldText, e.newText)) {
        if (l.side == Side::Added) ++e.additions;
        if (l.side == Side::Removed) ++e.deletions;
    }
    return e;
}

struct File {
    std::string key;   // node:path, or the path when no node was recorded
    std::string path;
    std::string node;
    std::vector<Edit> edits;  // oldest first

    [[nodiscard]] int additions() const {
        int n = 0;
        for (const Edit& e : edits) n += e.additions;
        return n;
    }
    [[nodiscard]] int deletions() const {
        int n = 0;
        for (const Edit& e : edits) n += e.deletions;
        return n;
    }
    [[nodiscard]] std::string name() const {
        const std::size_t slash = path.rfind('/');
        const std::string last = slash == std::string::npos ? path : path.substr(slash + 1);
        return last.empty() ? path : last;
    }
    [[nodiscard]] std::string directory() const {
        const std::size_t slash = path.rfind('/');
        return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
    }
};

// One settled tool call as the transcript holds it.
struct Call {
    std::string tool;    // the wire name
    std::string input;   // the tool's raw JSON argument object
    bool applied = false;
    std::string node;
};

inline std::string first_string(const nlohmann::json& in,
                                std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const auto it = in.find(k);
        if (it != in.end() && it->is_string()) {
            const std::string v = it->get<std::string>();
            if (v.find_first_not_of(" \t\n") != std::string::npos) return v;
        }
    }
    return {};
}

struct Change {
    std::string path;
    Edit edit;
};

// The change one applied call made, or none.
inline bool change_of(const Call& call, Change* out) {
    if (!call.applied) return false;
    const tool_kinds::Kind kind = tool_kinds::classify(call.tool);
    if (kind != tool_kinds::Kind::Edit && kind != tool_kinds::Kind::Write) return false;
    nlohmann::json in = nlohmann::json::parse(call.input, nullptr, false);
    if (in.is_discarded() || !in.is_object()) return false;
    if (in.contains("input") && in["input"].is_object()) in = in["input"];
    const std::string path = first_string(in, {"file_path", "path", "notebook_path"});
    if (path.empty()) return false;
    if (kind == tool_kinds::Kind::Write) {
        const auto it = in.find("content");
        if (it == in.end() || !it->is_string()) return false;
        *out = Change{path, make_edit("", it->get<std::string>(), false, true)};
        return true;
    }
    const auto o = in.find("old_string");
    const auto n = in.find("new_string");
    if (o == in.end() || n == in.end() || !o->is_string() || !n->is_string()) return false;
    const bool all = in.value("replace_all", false);
    *out = Change{path, make_edit(o->get<std::string>(), n->get<std::string>(), all, false)};
    return true;
}

// Most recently changed file first.
inline std::vector<File> fold(const std::vector<Call>& calls) {
    std::unordered_map<std::string, File> byKey;
    std::vector<std::string> order;
    for (const Call& call : calls) {
        Change c;
        if (!change_of(call, &c)) continue;
        const std::string key = call.node.empty() ? c.path : call.node + ":" + c.path;
        auto it = byKey.find(key);
        if (it == byKey.end())
            it = byKey.emplace(key, File{key, c.path, call.node, {}}).first;
        it->second.edits.push_back(std::move(c.edit));
        order.erase(std::remove(order.begin(), order.end(), key), order.end());
        order.insert(order.begin(), key);
    }
    std::vector<File> out;
    out.reserve(order.size());
    for (const std::string& k : order) out.push_back(std::move(byKey[k]));
    return out;
}

// The whole file as the transcript last left it, or false when it cannot be
// known exactly.
inline bool whole_text(const File& f, std::string* out) {
    std::size_t start = f.edits.size();
    for (std::size_t i = f.edits.size(); i-- > 0;)
        if (f.edits[i].writesWhole) {
            start = i;
            break;
        }
    if (start == f.edits.size()) return false;
    std::string text = f.edits[start].newText;
    for (std::size_t i = start + 1; i < f.edits.size(); ++i) {
        const Edit& e = f.edits[i];
        if (e.oldText.empty()) return false;
        std::size_t hits = 0;
        for (std::size_t at = text.find(e.oldText); at != std::string::npos;
             at = text.find(e.oldText, at + e.oldText.size()))
            ++hits;
        if (!(hits == 1 || (e.replaceAll && hits > 0))) return false;
        std::string next;
        std::size_t from = 0;
        for (std::size_t at = text.find(e.oldText); at != std::string::npos;
             at = text.find(e.oldText, from)) {
            next.append(text, from, at - from);
            next += e.newText;
            from = at + e.oldText.size();
            if (!e.replaceAll) break;
        }
        next.append(text, from, std::string::npos);
        text = std::move(next);
    }
    *out = std::move(text);
    return true;
}

struct Summary {
    int files = 0;
    int additions = 0;
    int deletions = 0;
};

inline Summary summary(const std::vector<File>& files) {
    Summary s;
    s.files = static_cast<int>(files.size());
    for (const File& f : files) {
        s.additions += f.additions();
        s.deletions += f.deletions();
    }
    return s;
}

inline std::string chip_label(const Summary& s, bool keepUnit = true) {
    if (!keepUnit) return std::to_string(s.files);
    return s.files == 1 ? "1 file" : std::to_string(s.files) + " files";
}

inline std::string header_label(const Summary& s) {
    return s.files == 1 ? "1 file changed" : std::to_string(s.files) + " files changed";
}

inline std::string counts(int additions, int deletions) {
    return "+" + std::to_string(additions) + " \xe2\x88\x92" + std::to_string(deletions);
}

}  // namespace api::changes
