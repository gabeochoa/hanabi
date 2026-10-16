#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::changed_files {

enum class Mark { Added, Modified };

inline char letter(Mark m) { return m == Mark::Added ? 'A' : 'M'; }

template <typename File>
Mark mark_of(const File& f) {
    bool allWhole = !f.edits.empty();
    for (const auto& e : f.edits)
        if (!e.writesWhole) allWhole = false;
    return allWhole ? Mark::Added : Mark::Modified;
}

enum class Group { Docs, Code, Tests };

inline std::string_view group_label(Group g) {
    switch (g) {
        case Group::Docs: return "Docs";
        case Group::Code: return "Code";
        case Group::Tests: return "Tests";
    }
    return "Code";
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

inline Group group_of(std::string_view path) {
    const std::string p = lower(path);
    const auto has = [&](std::string_view needle) { return p.find(needle) != std::string::npos; };
    const std::size_t slash = p.rfind('/');
    const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
    if (has("/test/") || has("/tests/") || p.rfind("test/", 0) == 0 || p.rfind("tests/", 0) == 0 ||
        has("_test.") || has(".test.") || has("/spec/") || p.rfind("spec/", 0) == 0 ||
        has("_spec.") || has(".spec.") || name.rfind("test_", 0) == 0)
        return Group::Tests;
    if (p.ends_with(".md") || p.ends_with(".txt") || p.ends_with(".rst") || p.ends_with(".adoc") ||
        has("docs/") || has("/doc/") || p.rfind("doc/", 0) == 0 || p.ends_with("readme") ||
        p.ends_with("changelog"))
        return Group::Docs;
    return Group::Code;
}

inline std::string common_prefix(const std::vector<std::string>& paths) {
    if (paths.size() < 2) return {};
    std::string prefix = paths.front();
    for (const std::string& p : paths) {
        std::size_t i = 0;
        while (i < prefix.size() && i < p.size() && prefix[i] == p[i]) ++i;
        prefix.resize(i);
    }
    const std::size_t slash = prefix.rfind('/');
    if (slash == std::string::npos) return {};
    return prefix.substr(0, slash + 1);
}

inline bool directory_first(std::string_view a, std::string_view b) {
    const bool ad = a.find('/') != std::string_view::npos;
    const bool bd = b.find('/') != std::string_view::npos;
    if (ad != bd) return !ad;
    return a < b;
}

struct Row {
    std::size_t index;
    Group group;
    Mark mark;
    std::string shown;
};

template <typename File>
std::vector<Row> rows(const std::vector<File>& files) {
    std::vector<std::string> paths;
    for (const File& f : files) paths.push_back(f.path);
    const std::string prefix = common_prefix(paths);
    std::vector<Row> out;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const File& f = files[i];
        std::string shown = f.path;
        if (!prefix.empty() && shown.rfind(prefix, 0) == 0) shown = shown.substr(prefix.size());
        out.push_back(Row{i, group_of(f.path), mark_of(f), shown});
    }
    std::stable_sort(out.begin(), out.end(), [](const Row& a, const Row& b) {
        if (a.group != b.group) return static_cast<int>(a.group) < static_cast<int>(b.group);
        return directory_first(a.shown, b.shown);
    });
    return out;
}

}
