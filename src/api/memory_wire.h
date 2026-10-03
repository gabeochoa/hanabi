#pragma once

#include <cstdlib>

#include <cctype>

// Agent memory over the agentcloud web app's GraphQL route (the reference's
// 0.8.4 Settings > Memory; its AgentMemoryStore / AgentcloudNestGraphQL).
//
// THE WIRE. POST http://<agentcloud host>/api/graphql with the same
// `intern_cat_token` cookie the inbox-state route takes (a CAT for
// XInternNestGraphQLThriftController), body
//   {"query_text": <document>, "variables": <variables as a JSON STRING>}
// -- a string, not an object, because one reading of the route refuses an
// object and the string satisfies both. The answer is two envelopes deep:
//   {"success": true, "result": {"data": {<field>: {...}}, "errors": [...]}}
//
// THE NAMESPACE. Personal memory is usecase `metamate_personal` (the server
// picks the root namespace for the viewer); a folder is a `sub_namespace`
// path, "" for the root. A file is a key with no '/' in it. Writes say
// CREATE_ONLY for a new file and UPDATE_ONLY with the version that was read
// for an edit, so a save never silently overwrites someone else's change.
//
// Every answer is checked against what was asked (usecase, folder, key)
// before it is believed: a reply for another folder is malformed, not data.
//
// Pure: no HTTP here, so every rule is reachable from a unit test.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::memory {

using json = nlohmann::json;

inline constexpr const char* kUsecase = "metamate_personal";
inline constexpr std::size_t kMaxBytes = 256 * 1024;
inline constexpr const char* kRoute = "/api/graphql";

inline constexpr const char* kListDoc =
    "query HanabiMemoryDirectory($input: MemexListMemoryNamespaceInput!) { "
    "memex_memory_namespace(input: $input) { usecase root_namespace sub_namespace "
    "memories { key content_length updated_at is_link } sub_namespaces next_cursor } }";
inline constexpr const char* kReadDoc =
    "query HanabiMemoryFile($input: MemexReadMemoryInput!) { memex_memory(input: $input) "
    "{ key content content_length version is_link usecase root_namespace sub_namespace } }";
inline constexpr const char* kWriteDoc =
    "mutation HanabiMemoryWrite($input: MemexWriteMemoryInput!) { memex_memory_write(input: "
    "$input) { key content content_length version is_link usecase root_namespace "
    "sub_namespace } }";

// A folder path: "" (the root) or '/'-joined segments, none empty, '.', or
// '..', no backslash, no control characters, at most 1 KiB.
inline bool valid_path(std::string_view path) {
    if (path.empty()) return true;
    if (path.size() > 1024) return false;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        const std::string_view seg =
            path.substr(start, slash == std::string_view::npos ? std::string_view::npos
                                                               : slash - start);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    for (const char c : path)
        if (c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

inline bool valid_key(std::string_view key) {
    return !key.empty() && key.find('/') == std::string_view::npos && valid_path(key) &&
           key != ".placeholder" && key != ".memex_keep";
}

inline json input_for(const std::string& path) {
    return json{{"usecase", kUsecase}, {"sub_namespace", path}};
}

// The request body for one document and its input.
inline std::string body(const char* document, const json& input) {
    return json{{"query_text", document}, {"variables", json{{"input", input}}.dump()}}.dump();
}

inline std::string list_body(const std::string& path, const std::string& cursor = {}) {
    json in = input_for(path);
    in["limit"] = 100;
    if (!cursor.empty()) in["cursor"] = cursor;
    return body(kListDoc, in);
}

inline std::string read_body(const std::string& path, const std::string& key) {
    json in = input_for(path);
    in["key"] = key;
    return body(kReadDoc, in);
}

// `version` empty = a new file (CREATE_ONLY); otherwise an edit of exactly
// that version (UPDATE_ONLY).
inline std::string write_body(const std::string& path, const std::string& key,
                              const std::string& content, const std::string& version) {
    json in = input_for(path);
    in["key"] = key;
    in["content"] = content;
    in["mode"] = version.empty() ? "CREATE_ONLY" : "UPDATE_ONLY";
    if (!version.empty()) in["expected_version"] = version;
    return body(kWriteDoc, in);
}

struct File {
    std::string key;
    std::int64_t bytes = -1;  // -1 = not reported
    bool linked = false;
};

struct Listing {
    std::string root;     // the namespace the server resolved for the viewer
    std::string path;     // the folder listed
    std::vector<File> files;
    std::vector<std::string> folders;  // full paths
    std::string cursor;   // "" = no more
};

struct Document {
    std::string key;
    std::string content;
    std::string version;  // "" = the server gave none (cannot be saved over)
    bool linked = false;
};

// Opens the route's two envelopes down to the GraphQL `data` object.
inline bool unwrap_data(const std::string& raw, json* out, std::string* why) {
    const json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        *why = "not JSON";
        return false;
    }
    if (!j.value("success", false) || !j.contains("result") || !j["result"].is_object() ||
        !j["result"].contains("data") || !j["result"]["data"].is_object()) {
        *why = "no success/result/data envelope";
        if (j.contains("result") && j["result"].is_object() && j["result"].contains("errors") &&
            j["result"]["errors"].is_array() && !j["result"]["errors"].empty())
            *why += " (" + j["result"]["errors"][0].value("message", std::string("error")) + ")";
        return false;
    }
    *out = j["result"]["data"];
    return true;
}

// Opens the route's two envelopes down to `field`'s object, or returns the
// reason it could not (which is logged; the reader sees a plain sentence).
inline bool unwrap(const std::string& raw, const char* field, json* out, std::string* why) {
    const json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        *why = "not JSON";
        return false;
    }
    if (!j.value("success", false) || !j.contains("result") || !j["result"].is_object()) {
        *why = "no success/result envelope";
        return false;
    }
    const json& result = j["result"];
    if (!result.contains("data") || !result["data"].is_object() ||
        !result["data"].contains(field) || !result["data"][field].is_object()) {
        *why = std::string("no data.") + field;
        if (result.contains("errors") && result["errors"].is_array() && !result["errors"].empty())
            *why += " (" + result["errors"][0].value("message", std::string("error")) + ")";
        return false;
    }
    *out = result["data"][field];
    return true;
}

// A refused memory call, read for the causes a retry cannot fix (the
// reference's AgentMemoryError.classifyWrite, D122987004 + D123014515). The
// failure arrives as Hanabi's own string ("memory HTTP <n>", or the service's
// words in "no data.<field> (<message>)"). Status decides first; then the
// service's words, spelled the way the web app's memory route reads them.
enum class Failure { Server, VersionConflict, AlreadyExists, NotPermitted, SignInExpired, NoAccess };

inline int http_status_of(const std::string& error) {
    static const std::string kTag = "memory HTTP ";
    const auto at = error.find(kTag);
    if (at == std::string::npos) return 0;
    return std::atoi(error.c_str() + at + kTag.size());
}

inline Failure classify_write(const std::string& error) {
    const int status = http_status_of(error);
    std::string words;
    words.reserve(error.size());
    for (char c : error) words += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (words.find("protected_mode_operation_denied") != std::string::npos || status == 403)
        return Failure::NotPermitted;
    if (status == 401) return Failure::SignInExpired;  // already re-minted once
    if (status >= 500) return Failure::Server;
    if (words.find("versionconflict") != std::string::npos ||
        (words.find("version") != std::string::npos &&
         (words.find("conflict") != std::string::npos || words.find("expected") != std::string::npos)))
        return Failure::VersionConflict;
    if (words.find("alreadyexists") != std::string::npos ||
        words.find("already exists") != std::string::npos || words.find("duplicate") != std::string::npos)
        return Failure::AlreadyExists;
    if (words.find("permissiondenied") != std::string::npos ||
        words.find("permission denied") != std::string::npos ||
        words.find("read-only") != std::string::npos || words.find("not permitted") != std::string::npos)
        return Failure::NotPermitted;
    return Failure::Server;
}

// A read: only the status says anything a retry cannot fix.
inline Failure classify_read(const std::string& error) {
    const int status = http_status_of(error);
    if (status == 401) return Failure::SignInExpired;
    if (status == 403) return Failure::NoAccess;
    return Failure::Server;
}

// What to do instead of "try again", or nullptr for a Server failure (where
// "try again in a moment" is the honest advice).
inline const char* sentence(Failure f) {
    switch (f) {
        case Failure::VersionConflict:
            return "This file changed since you opened it. Copy your edits, reopen the file, "
                   "and apply them again.";
        case Failure::AlreadyExists:
            return "A file or folder with that name already exists here. Pick another name.";
        case Failure::NotPermitted:
            return "You can\xe2\x80\x99t change memory here. This context is read-only for you.";
        case Failure::SignInExpired:
            return "Hanabi\xe2\x80\x99s sign-in expired and could not be renewed. Quit and reopen "
                   "Hanabi, then try again.";
        case Failure::NoAccess: return "You don\xe2\x80\x99t have access to this memory.";
        case Failure::Server: break;
    }
    return nullptr;
}

inline bool same_context(const json& v, const std::string& path) {
    return v.value("usecase", std::string()) == kUsecase &&
           v.value("sub_namespace", std::string()) == path &&
           !v.value("root_namespace", std::string()).empty();
}

inline std::optional<Listing> parse_listing(const json& v, const std::string& path) {
    if (!same_context(v, path)) return std::nullopt;
    if (!v.contains("memories") || !v["memories"].is_array() || !v.contains("sub_namespaces") ||
        !v["sub_namespaces"].is_array())
        return std::nullopt;
    Listing out;
    out.root = v.value("root_namespace", std::string());
    out.path = path;
    for (const json& row : v["memories"]) {
        if (!row.is_object() || !row.contains("key") || !row["key"].is_string())
            return std::nullopt;
        const std::string key = row["key"].get<std::string>();
        if (!valid_key(key)) return std::nullopt;
        bool dup = false;
        for (const File& f : out.files) dup = dup || f.key == key;
        if (dup) continue;
        File f;
        f.key = key;
        if (row.contains("content_length") && row["content_length"].is_number_integer())
            f.bytes = row["content_length"].get<std::int64_t>();
        f.linked = row.value("is_link", false);
        out.files.push_back(std::move(f));
    }
    for (const json& d : v["sub_namespaces"]) {
        if (!d.is_string()) return std::nullopt;
        const std::string dir = d.get<std::string>();
        if (dir.empty() || !valid_path(dir)) return std::nullopt;
        if (!path.empty() && dir.rfind(path + "/", 0) != 0) return std::nullopt;
        out.folders.push_back(dir);
    }
    std::sort(out.folders.begin(), out.folders.end());
    out.folders.erase(std::unique(out.folders.begin(), out.folders.end()), out.folders.end());
    if (v.contains("next_cursor") && v["next_cursor"].is_string())
        out.cursor = v["next_cursor"].get<std::string>();
    return out;
}

inline std::optional<Document> parse_document(const json& v, const std::string& path,
                                              const std::string& key) {
    if (!same_context(v, path)) return std::nullopt;
    if (v.value("key", std::string()) != key || !v.contains("content") ||
        !v["content"].is_string())
        return std::nullopt;
    Document d;
    d.key = key;
    d.content = v["content"].get<std::string>();
    if (d.content.size() > kMaxBytes) return std::nullopt;
    if (v.contains("version") && v["version"].is_string()) d.version = v["version"].get<std::string>();
    d.linked = v.value("is_link", false);
    return d;
}

// The last segment of a folder path, for its row.
inline std::string folder_name(const std::string& dir) {
    const std::size_t slash = dir.rfind('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

// The folder above `path`, "" at or under the root.
inline std::string parent_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace api::memory
