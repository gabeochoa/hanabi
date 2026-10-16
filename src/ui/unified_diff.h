#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::unified_diff {

enum class Kind { Header, Hunk, Added, Removed, Context, Meta };

struct Line {
    Kind kind;
    std::string text;
};

struct File {
    std::string old_path;
    std::string new_path;
    int additions = 0;
    int deletions = 0;
    std::string title() const {
        if (!new_path.empty() && new_path != "/dev/null") return new_path;
        if (!old_path.empty() && old_path != "/dev/null") return old_path;
        return {};
    }
};

struct Parse {
    bool ok = false;
    std::vector<File> files;
    std::vector<Line> lines;
    int additions = 0;
    int deletions = 0;
    std::string title() const {
        if (files.size() == 1 && !files.front().title().empty()) return files.front().title();
        if (files.size() > 1) return std::to_string(files.size()) + " files";
        return "Code changes";
    }
};

inline std::string strip_path_prefix(std::string_view p) {
    std::string_view v = p;
    const std::size_t tab = v.find('\t');
    if (tab != std::string_view::npos) v = v.substr(0, tab);
    while (!v.empty() && v.back() == ' ') v.remove_suffix(1);
    if (v.rfind("a/", 0) == 0 || v.rfind("b/", 0) == 0) v.remove_prefix(2);
    return std::string(v);
}

inline bool is_lang(std::string_view lang) { return lang == "diff" || lang == "patch"; }

inline bool looks_like(std::string_view body) {
    bool sawOld = false, sawNew = false, sawHunk = false;
    std::size_t at = 0;
    while (at <= body.size()) {
        const std::size_t nl = body.find('\n', at);
        const std::string_view line = body.substr(at, (nl == std::string_view::npos ? body.size() : nl) - at);
        if (line.rfind("--- ", 0) == 0) sawOld = true;
        else if (line.rfind("+++ ", 0) == 0) sawNew = true;
        else if (line.rfind("@@", 0) == 0) sawHunk = true;
        if (nl == std::string_view::npos) break;
        at = nl + 1;
    }
    return sawHunk && (sawOld || sawNew);
}

inline Parse parse(std::string_view body) {
    Parse out;
    if (!looks_like(body)) return out;
    File* cur = nullptr;
    bool inHunk = false;
    std::size_t at = 0;
    while (at <= body.size()) {
        const std::size_t nl = body.find('\n', at);
        const std::string_view line = body.substr(at, (nl == std::string_view::npos ? body.size() : nl) - at);
        if (line.rfind("--- ", 0) == 0) {
            out.files.push_back(File{});
            cur = &out.files.back();
            cur->old_path = strip_path_prefix(line.substr(4));
            inHunk = false;
            out.lines.push_back({Kind::Header, std::string(line)});
        } else if (line.rfind("+++ ", 0) == 0) {
            if (cur == nullptr) {
                out.files.push_back(File{});
                cur = &out.files.back();
            }
            cur->new_path = strip_path_prefix(line.substr(4));
            out.lines.push_back({Kind::Header, std::string(line)});
        } else if (line.rfind("@@", 0) == 0) {
            inHunk = true;
            out.lines.push_back({Kind::Hunk, std::string(line)});
        } else if (inHunk && !line.empty() && line[0] == '+') {
            out.lines.push_back({Kind::Added, std::string(line)});
            ++out.additions;
            if (cur) ++cur->additions;
        } else if (inHunk && !line.empty() && line[0] == '-') {
            out.lines.push_back({Kind::Removed, std::string(line)});
            ++out.deletions;
            if (cur) ++cur->deletions;
        } else if (inHunk && (line.empty() || line[0] == ' ' || line[0] == '\\')) {
            out.lines.push_back({Kind::Context, std::string(line)});
        } else {
            inHunk = false;
            out.lines.push_back({Kind::Meta, std::string(line)});
        }
        if (nl == std::string_view::npos) break;
        at = nl + 1;
    }
    while (!out.lines.empty() && out.lines.back().kind == Kind::Context && out.lines.back().text.empty())
        out.lines.pop_back();
    out.ok = !out.lines.empty();
    return out;
}

inline std::string counts(int additions, int deletions) {
    return "+" + std::to_string(additions) + " \xe2\x88\x92" + std::to_string(deletions);
}

}
