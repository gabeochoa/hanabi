#pragma once

// The viewer's Metamate Spaces, read from the agentcloud web app's
// /api/graphql (the reference's AgentcloudSpaces; the query is the web's
// `metamateSpacesQuery.ts` field selection) -- for the @ picker's Space rows.
//
// One round trip, no paging: a viewer has tens of Spaces, so `first: 100`
// is the whole set; archived ones are excluded server-side. A named agent is
// a project but not a Space, and is dropped. Order: pinned first, then
// Metamate's own viewer_rank (nulls last), then name, then id.
//
// Pure: the HTTP lives in the client (memory_post's route and credential).

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::spaces {

using json = nlohmann::json;

inline constexpr const char* kDoc =
    "query HanabiViewerMetamateSpaces { viewer_intern_user { metamate_projects(first: 100, "
    "is_archived: false) { edges { node { id name emoji project_type if_viewer_has_pinned "
    "viewer_rank } } } } }";
inline constexpr const char* kNamedAgent = "NAMED_AGENT";

struct Space {
    std::string id;
    std::string name;
    std::string emoji;
    bool pinned = false;
    int rank = -1;  // -1 = none
};

// What a mention writes for a Space: `space:<id> ` -- an fbid of 1-64 ASCII
// digits, checked before it is ever put on the wire.
inline bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id)
        if (c < '0' || c > '9') return false;
    return true;
}

inline std::string body() { return json{{"query_text", kDoc}}.dump(); }

// Which Space each of the viewer's sessions is filed in: one paged walk of
// the session list with no filter (a child inherits its parent's container,
// so the walk includes them), the web's sessionSpaceQuery shape. The cursor
// rides as a variable so every page reuses one persisted document.
inline constexpr const char* kIndexDoc =
    "query HanabiSessionSpaceIndex($first: Int!, $after: String) { "
    "xfb_agentcloud_session_list_for_viewer(first: $first, after: $after) { sessions { "
    "session_id space { id } } next_cursor } }";
inline constexpr int kIndexPageSize = 200;
inline constexpr int kIndexMaxPages = 25;

inline std::string index_body(const std::string& after) {
    json vars{{"first", kIndexPageSize}, {"after", after.empty() ? json() : json(after)}};
    return json{{"query_text", kIndexDoc}, {"variables", vars.dump()}}.dump();
}

struct IndexPage {
    std::vector<std::pair<std::string, std::string>> filed;  // session -> space
    std::string next;                                         // "" = the end
};

// Positive evidence only: an unfiled row, or one whose Space the viewer may
// not see, is simply absent. An unreadable answer ENDS the walk.
inline IndexPage parse_index(const json& data) {
    IndexPage out;
    if (!data.is_object() || !data.contains("xfb_agentcloud_session_list_for_viewer")) return out;
    const json& p = data["xfb_agentcloud_session_list_for_viewer"];
    if (!p.is_object()) return out;
    if (p.contains("sessions") && p["sessions"].is_array())
        for (const json& row : p["sessions"]) {
            if (!row.is_object() || !row.contains("session_id") || !row["session_id"].is_string())
                continue;
            const std::string sid = row["session_id"].get<std::string>();
            if (sid.empty() || !row.contains("space") || !row["space"].is_object()) continue;
            const json& sp = row["space"];
            if (!sp.contains("id") || !sp["id"].is_string()) continue;
            const std::string id = sp["id"].get<std::string>();
            if (!valid_id(id)) continue;
            out.filed.emplace_back(sid, id);
        }
    if (p.contains("next_cursor") && p["next_cursor"].is_string())
        out.next = p["next_cursor"].get<std::string>();
    return out;
}

// `data` (inside the route's two envelopes) -> the Spaces, ordered.
inline std::optional<std::vector<Space>> parse(const json& data) {
    if (!data.is_object() || !data.contains("viewer_intern_user")) return std::nullopt;
    const json& viewer = data["viewer_intern_user"];
    if (!viewer.is_object() || !viewer.contains("metamate_projects")) return std::nullopt;
    const json& projects = viewer["metamate_projects"];
    if (!projects.is_object() || !projects.contains("edges") || !projects["edges"].is_array())
        return std::nullopt;
    std::vector<Space> out;
    for (const json& edge : projects["edges"]) {
        if (!edge.is_object() || !edge.contains("node") || !edge["node"].is_object()) continue;
        const json& n = edge["node"];
        if (!n.contains("id") || !n["id"].is_string()) continue;
        Space s;
        s.id = n["id"].get<std::string>();
        if (!valid_id(s.id)) continue;
        if (n.value("project_type", std::string()) == kNamedAgent) continue;
        s.name = n.value("name", std::string());
        s.emoji = n.contains("emoji") && n["emoji"].is_string() ? n["emoji"].get<std::string>() : "";
        s.pinned = n.contains("if_viewer_has_pinned") && n["if_viewer_has_pinned"].is_boolean() &&
                   n["if_viewer_has_pinned"].get<bool>();
        if (n.contains("viewer_rank") && n["viewer_rank"].is_number_integer())
            s.rank = n["viewer_rank"].get<int>();
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(), [](const Space& a, const Space& b) {
        if (a.pinned != b.pinned) return a.pinned;
        if ((a.rank >= 0) != (b.rank >= 0)) return a.rank >= 0;
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.name != b.name) return a.name < b.name;
        return a.id < b.id;
    });
    return out;
}

}  // namespace api::spaces
