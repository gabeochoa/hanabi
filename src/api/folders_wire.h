#pragma once

// The viewer's sidebar FOLDERS as the web app keeps them (the reference's
// SessionFolders; Knots kt-im8t): named, ordered, personal groups of
// threads -- `session_folders` rows plus a `folderId` on the per-viewer
// session overlay row. Hanabi reads that state rather than keeping a second
// copy, so a folder made on the web appears here and a thread filed on the web
// is filed here.
//
//   GET    /api/session-folders        -> { folders: [{ id, name, position }] }
//   GET    /api/session-overlay        -> { overlays: [{ sessionId, folderId }] }
//   POST   /api/session-folders        <- { name }          -> { folder: {...} }
//   PATCH  /api/session-folders/{id}   <- { name }
//   DELETE /api/session-folders/{id}
//   POST   /api/session-overlay        <- { sessionId, folderId | null }
//
// All on the web app's origin with the intern_cat_token cookie and the
// `x-agentcloud-csrf: 1` header (the inbox route's lane). A refusal answers
// `{ "error": "..." }`, which is the sentence worth showing. Pure.

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::folders {

using json = nlohmann::json;

inline constexpr const char* kFoldersPath = "/api/session-folders";
inline constexpr const char* kOverlayPath = "/api/session-overlay";
inline constexpr std::size_t kNameLimit = 100;  // createFolderSchema / patchFolderSchema

struct Folder {
    std::string id;
    std::string name;
    int position = 0;
    bool operator==(const Folder&) const = default;
};

// What the sidebar draws: the folders in draw order (position, then name,
// then id) and which folder each session is in -- a membership naming a
// folder not in the list is dropped, so a folder deleted between the two
// reads never makes a section nobody can name.
struct Carve {
    std::vector<Folder> folders;
    std::unordered_map<std::string, std::string> membership;  // session -> folder
    bool operator==(const Carve&) const = default;
    const Folder* find(const std::string& id) const {
        for (const Folder& f : folders)
            if (f.id == id) return &f;
        return nullptr;
    }
};

inline std::optional<std::vector<Folder>> parse_folders(const std::string& body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("folders") ||
        !root["folders"].is_array())
        return std::nullopt;
    std::vector<Folder> out;
    for (const json& row : root["folders"]) {
        if (!row.is_object()) continue;
        const std::string id = row.value("id", std::string());
        const std::string name = row.value("name", std::string());
        if (id.empty() || name.empty()) continue;
        out.push_back({id, name, row.contains("position") && row["position"].is_number_integer()
                                     ? row["position"].get<int>()
                                     : 0});
    }
    return out;
}

inline std::optional<std::unordered_map<std::string, std::string>> parse_membership(
    const std::string& body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("overlays") ||
        !root["overlays"].is_array())
        return std::nullopt;
    std::unordered_map<std::string, std::string> out;
    for (const json& row : root["overlays"]) {
        if (!row.is_object() || !row.contains("folderId") || !row["folderId"].is_string()) continue;
        const std::string sid = row.value("sessionId", std::string());
        const std::string fid = row["folderId"].get<std::string>();
        if (!sid.empty() && !fid.empty()) out[sid] = fid;
    }
    return out;
}

inline Carve carve(std::vector<Folder> folders,
                   const std::unordered_map<std::string, std::string>& membership) {
    std::stable_sort(folders.begin(), folders.end(), [](const Folder& a, const Folder& b) {
        if (a.position != b.position) return a.position < b.position;
        if (a.name != b.name) return a.name < b.name;
        return a.id < b.id;
    });
    Carve c;
    std::set<std::string> known;
    for (const Folder& f : folders) known.insert(f.id);
    c.folders = std::move(folders);
    for (const auto& [sid, fid] : membership)
        if (known.count(fid) != 0) c.membership[sid] = fid;
    return c;
}

inline std::optional<Folder> parse_created(const std::string& body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("folder") ||
        !root["folder"].is_object())
        return std::nullopt;
    const json& row = root["folder"];
    const std::string id = row.value("id", std::string());
    const std::string name = row.value("name", std::string());
    if (id.empty() || name.empty()) return std::nullopt;
    return Folder{id, name,
                  row.contains("position") && row["position"].is_number_integer()
                      ? row["position"].get<int>()
                      : 0};
}

// A name the routes accept (trimmed, 1..100), or the sentence to show.
struct NameCheck {
    std::string name;
    std::string error;  // "" = fine
};
inline NameCheck validate_name(std::string_view raw) {
    std::size_t a = 0, b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) --b;
    NameCheck out;
    out.name = std::string(raw.substr(a, b - a));
    if (out.name.empty()) out.error = "A folder needs a name.";
    else if (out.name.size() > kNameLimit)
        out.error = "A folder name can be at most 100 characters.";
    return out;
}

// The server's own sentence when it sent one ({"error": "..."}), else
// `fallback`.
inline std::string refusal(const std::string& body, const std::string& fallback) {
    const json root = json::parse(body, nullptr, false);
    if (!root.is_discarded() && root.is_object() && root.contains("error") &&
        root["error"].is_string() && !root["error"].get<std::string>().empty())
        return root["error"].get<std::string>();
    return fallback;
}

inline std::string name_body(const std::string& name) { return json{{"name", name}}.dump(); }
// "out of every folder" is an explicit null: the route refuses a body that
// names nothing to update.
inline std::string file_body(const std::string& session_id, const std::string& folder_id) {
    return json{{"sessionId", session_id},
                {"folderId", folder_id.empty() ? json() : json(folder_id)}}
        .dump();
}

}  // namespace api::folders
