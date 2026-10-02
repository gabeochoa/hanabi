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
