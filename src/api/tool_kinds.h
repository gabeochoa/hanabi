#pragma once

// The tool kinds this fleet actually runs, and how each reads in a transcript.
//
// A tool row used to be one shape for every tool: a wrench, the tool's name,
// and whichever argument happened to be first in a fixed key list. That made
// {"file_path": "..."} the headline of every Read and every Edit, and gave
// a grep, a shell command and a web fetch the same mark. The registry below
// is the one place a tool name becomes a kind, a kind becomes a word, and an
// input becomes the line a reader wants. An unknown tool stays the generic
// block it always was.

#include <string>
#include <string_view>

#include "../../vendor/nlohmann/json.hpp"

namespace api::tool_kinds {

enum class Kind { Generic, Read, Write, Edit, Shell, Grep, Glob, SubAgent, Web };

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The platform's own tools are lowercase (read, bash, grep); the coding
// harnesses capitalise theirs (Read, Bash, WebFetch, Task) and prefix MCP
// tools with their server (subagent__spawn, web__fetch). All of those are
// the same eight kinds.
inline Kind classify(std::string_view tool_name) {
    const std::string n = lower(tool_name);
    const auto ends = [&](std::string_view suf) {
        return n.size() >= suf.size() &&
               n.compare(n.size() - suf.size(), suf.size(), suf) == 0;
    };
    const auto has = [&](std::string_view part) {
        return n.find(part) != std::string::npos;
    };
    if (n == "read" || n == "read_file" || ends("__read") || n == "notebookread")
        return Kind::Read;
    if (n == "write" || n == "write_file" || ends("__write")) return Kind::Write;
    if (n == "edit" || n == "multiedit" || n == "notebookedit" ||
        ends("__edit"))
        return Kind::Edit;
    if (n == "bash" || n == "shell" || n == "sh" || n == "python" ||
        ends("__bash"))
        return Kind::Shell;
    if (n == "grep" || n == "rg" || ends("__grep")) return Kind::Grep;
    if (n == "glob" || ends("__glob")) return Kind::Glob;
    if (n == "task" || n == "agent" || has("subagent")) return Kind::SubAgent;
    if (has("web") || has("fetch_url") || n == "browser") return Kind::Web;
    return Kind::Generic;
}

inline const char* word(Kind k) {
    switch (k) {
        case Kind::Read: return "read";
        case Kind::Write: return "write";
        case Kind::Edit: return "edit";
        case Kind::Shell: return "shell";
        case Kind::Grep: return "grep";
        case Kind::Glob: return "glob";
        case Kind::SubAgent: return "agent";
        case Kind::Web: return "web";
        case Kind::Generic: break;
    }
    return "tool";
}

namespace detail {
inline std::string str(const nlohmann::json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return {};
    const auto& v = j.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    return {};
}
inline std::string first_of(const nlohmann::json& j,
                            std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        std::string v = str(j, k);
        if (!v.empty()) return v;
    }
    return {};
}
}  // namespace detail

// The one line a reader wants from a call's arguments: the file, the
// command, the pattern and where it was searched. `raw` is the tool's input
// as the wire carries it, a JSON string of the argument object; anything
// that is not one comes back untouched, as does an object with nothing this
// kind knows how to read.
// What a row calls the tool. A bare name is what the fleet calls it, in
// lower case (bash stays bash, not "shell"); a folder name of a known kind is
// the kind's word (subagent__inspect reads "agent"); a folder name this table
// does not know is spelled as words (weather__forecast_daily reads "weather
// forecast daily"). The wire spelling itself never reaches the reader.
inline std::string display_name(std::string_view tool_name) {
    std::string out = lower(tool_name);
    const size_t sep = out.find("__");
    if (sep == std::string::npos) return out;
    const Kind k = classify(tool_name);
    if (k != Kind::Generic) return word(k);
    if (sep > 0 && sep + 2 < out.size()) out.replace(sep, 2, " ");
    for (char& c : out)
        if (c == '_') c = ' ';
    return out;
}

// The kind's word as a debug-name suffix, so a scripted test can reach the
// mark a row drew; empty for Generic, whose names stay as they were.
inline std::string debug_suffix(Kind k) {
    return k == Kind::Generic ? std::string() : std::string("_") + word(k);
}

inline std::string headline(Kind kind, const std::string& raw) {
    using detail::first_of;
    using detail::str;
    const nlohmann::json in = nlohmann::json::parse(raw, nullptr, false);
    if (in.is_discarded() || !in.is_object()) return raw;
    std::string out;
    switch (kind) {
        case Kind::Read: {
            out = first_of(in, {"file_path", "path", "notebook_path"});
            const std::string offset = str(in, "offset");
            const std::string limit = str(in, "limit");
            if (!out.empty() && !offset.empty() && !limit.empty()) {
                const long long lo = std::stoll(offset) + 1;
                const long long hi = std::stoll(offset) + std::stoll(limit);
                out += "  lines " + std::to_string(lo) + "\xe2\x80\x93" +
                       std::to_string(hi);
            }
            break;
        }
        case Kind::Write:
        case Kind::Edit:
            out = first_of(in, {"file_path", "path", "notebook_path"});
            break;
        case Kind::Shell:
            out = first_of(in, {"command", "code", "cmd"});
            break;
        case Kind::Grep: {
            out = first_of(in, {"pattern", "query", "regex"});
            const std::string where = first_of(in, {"path", "include", "glob"});
            if (!out.empty() && !where.empty()) out += "  in " + where;
            break;
        }
        case Kind::Glob: {
            out = first_of(in, {"pattern", "glob"});
            const std::string where = str(in, "path");
            if (!out.empty() && !where.empty()) out += "  in " + where;
            break;
        }
        case Kind::SubAgent:
            out = first_of(in, {"title", "description", "prompt"});
            break;
        case Kind::Web:
            out = first_of(in, {"url", "query", "prompt"});
            break;
        case Kind::Generic:
            break;
    }
    if (out.empty())
        out = first_of(in, {"command", "text", "query", "path", "file_path",
                            "pattern", "url"});
    return out.empty() ? raw : out;
}

}  // namespace api::tool_kinds
