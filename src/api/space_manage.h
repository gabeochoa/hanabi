#pragma once

// Space settings (the reference's SpaceSettingsSheet + AgentcloudSpaceMembers
// + AgentcloudSpaceManage + AgentcloudPeopleSearch, kt-h76c / kt-fvjo;
// puffin_gaps.md D10): one Space's roster, the viewer's own fbid, the people
// search the member picker runs, and Metamate's seven Space writes -- rename,
// icon, visibility, add member, remove member (and leave: the same mutation
// naming yourself), change role, archive.
//
// METAMATE IS THE AUTHORIZATION. Every write is Metamate's own mutation,
// admin-gated server-side, sent as the viewer over the web app's /api/graphql.
// The catalog's `if_viewer_can_write_to_project_as_admin` decides which
// CONTROLS are offered, never what is allowed: a refusal is rendered in the
// server's words. Nothing here decides who may do what.
//
// Every value travels as a GraphQL VARIABLE; the documents are constants.
//
// Pure: bodies and parses only. No fetch runs here.

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"
#include "spaces_wire.h"

namespace api::space_manage {

using json = nlohmann::json;

inline constexpr const char* kSurface = "agentcloud";
inline constexpr int kRosterPage = 100;

// ---- The roster ------------------------------------------------------------

struct Member {
    std::string id;    // the encoded graph node id: a row identity only
    std::string fbid;  // the raw employee fbid ("" = no Employee arm: drawn, not actionable)
    std::string name;
    std::string role;  // ADMIN / OWNER / MEMBER / SUBSCRIBER, untyped
    bool operator==(const Member&) const = default;
};

// ADMIN and OWNER are the same authority spelled two ways (a Space
// administered directly vs one backed by a team) -- one predicate, the web's
// kt-si4s fix carried across.
inline bool is_admin_role(std::string_view role) { return role == "ADMIN" || role == "OWNER"; }

inline constexpr const char* kRosterDoc =
    "query HanabiMetamateSpaceMembers($spaceId: ID!) { xfb_metamate_project(id: $spaceId) { "
    "team_members(first: 100) { count edges { role node { id name ... on Employee { fbid: "
    "unencoded_id } } } } } }";

inline std::string roster_body(const std::string& spaceId) {
    return json{{"query_text", kRosterDoc}, {"variables", json{{"spaceId", spaceId}}.dump()}}.dump();
}

struct Roster {
    std::vector<Member> members;
    int total = 0;  // Metamate's own count, which can exceed one page
};

// Admins first, then by name (case-insensitive), then by id.
inline void order(std::vector<Member>& m) {
    const auto lower = [](const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return o;
    };
    std::stable_sort(m.begin(), m.end(), [&](const Member& a, const Member& b) {
        const bool aa = is_admin_role(a.role), ba = is_admin_role(b.role);
        if (aa != ba) return aa;
        const std::string la = lower(a.name), lb = lower(b.name);
        if (la != lb) return la < lb;
        return a.id < b.id;
    });
}

inline std::optional<Roster> parse_roster(const json& data) {
    if (!data.is_object() || !data.contains("xfb_metamate_project") ||
        !data["xfb_metamate_project"].is_object())
        return std::nullopt;
    const json& p = data["xfb_metamate_project"];
    if (!p.contains("team_members") || !p["team_members"].is_object()) return std::nullopt;
    const json& team = p["team_members"];
    Roster r;
    if (team.contains("edges") && team["edges"].is_array())
        for (const json& e : team["edges"]) {
            if (!e.is_object() || !e.contains("node") || !e["node"].is_object()) continue;
            const json& n = e["node"];
            const std::string id = n.contains("id") && n["id"].is_string() ? n["id"].get<std::string>() : "";
            if (id.empty()) continue;
            Member m;
            m.id = id;
            const std::string fbid =
                n.contains("fbid") && n["fbid"].is_string() ? n["fbid"].get<std::string>() : "";
            m.fbid = spaces::valid_id(fbid) ? fbid : "";  // a malformed one cannot name a member
            m.name = n.contains("name") && n["name"].is_string() ? n["name"].get<std::string>() : "";
            m.role = e.contains("role") && e["role"].is_string() ? e["role"].get<std::string>() : "";
            r.members.push_back(std::move(m));
        }
    r.total = team.contains("count") && team["count"].is_number_integer()
                  ? team["count"].get<int>()
                  : static_cast<int>(r.members.size());
    order(r.members);
    return r;
}

// ---- Who the viewer is (Leave names yourself) ------------------------------

inline constexpr const char* kViewerDoc =
    "query HanabiViewerFbid { viewer_intern_user { intern_user { unencoded_id } } }";
inline std::string viewer_body() { return json{{"query_text", kViewerDoc}}.dump(); }
inline std::string parse_viewer(const json& data) {
    if (!data.is_object() || !data.contains("viewer_intern_user")) return {};
    const json& v = data["viewer_intern_user"];
    if (!v.is_object() || !v.contains("intern_user") || !v["intern_user"].is_object()) return {};
    const json& u = v["intern_user"];
    const std::string id =
        u.contains("unencoded_id") && u["unencoded_id"].is_string() ? u["unencoded_id"].get<std::string>() : "";
    return spaces::valid_id(id) ? id : std::string();
}

// ---- The people search (Add someone by name) -------------------------------

struct Person {
    std::string fbid;
    std::string name;
    std::string subtitle;
    bool operator==(const Person&) const = default;
};

inline constexpr std::size_t kMinQuery = 2;
inline constexpr int kMaxPeople = 6;

inline bool worth_searching(std::string_view q) {
    std::size_t n = 0;
    for (char c : q)
        if (!std::isspace(static_cast<unsigned char>(c))) ++n;
    return n >= kMinQuery;
}

// InternGraph prefix-matches the final token only from three characters, so a
// short last token is closed with a space ("DJ" finds nobody, "DJ " finds DJ);
// a long one is left alone (closing it would stop it prefix-matching).
inline std::string normalize_query(const std::string& raw) {
    if (raw.empty() || std::isspace(static_cast<unsigned char>(raw.back()))) return raw;
    std::size_t start = raw.find_last_of(" \t\n");
    start = start == std::string::npos ? 0 : start + 1;
    const std::size_t last = raw.size() - start;
    return last > 0 && last < 3 ? raw + " " : raw;
}

inline constexpr const char* kPeopleDoc =
    "query HanabiInternPeopleTypeahead($query: String!, $types: [InternTypeaheadType], $filter: "
    "String, $first: Int) { intern_typeahead_query(query: $query) { results(of_type: $types, "
    "filter: $filter, first: $first) { nodes { fbid title subtitle } } } }";

inline std::string people_body(const std::string& raw) {
    const json vars{{"query", normalize_query(raw)},
                    {"types", json::array({"INTERN_USER"})},
                    {"filter", "EMPLOYEE_ONLY"},
                    {"first", kMaxPeople}};
    return json{{"query_text", kPeopleDoc}, {"variables", vars.dump()}}.dump();
}

// A node with no usable fbid, or no name, is dropped: a search row exists to be
// picked.
inline std::optional<std::vector<Person>> parse_people(const json& data) {
    if (!data.is_object() || !data.contains("intern_typeahead_query")) return std::nullopt;
    const json& q = data["intern_typeahead_query"];
    if (!q.is_object() || !q.contains("results") || !q["results"].is_object()) return std::nullopt;
    std::vector<Person> out;
    const json& r = q["results"];
    if (r.contains("nodes") && r["nodes"].is_array())
        for (const json& n : r["nodes"]) {
            if (!n.is_object()) continue;
            const std::string fbid = n.contains("fbid") && n["fbid"].is_string() ? n["fbid"].get<std::string>() : "";
            const std::string name = n.contains("title") && n["title"].is_string() ? n["title"].get<std::string>() : "";
            if (!spaces::valid_id(fbid) || name.empty()) continue;
            out.push_back({fbid, name,
                           n.contains("subtitle") && n["subtitle"].is_string() ? n["subtitle"].get<std::string>() : ""});
        }
    return out;
}

// ---- The writes ------------------------------------------------------------

enum class Write { Rename, Emoji, Visibility, AddMember, RemoveMember, Leave, Role, Archive };

struct Request {
    std::string body;
    const char* field = "";  // the response key that says the write happened
};

inline constexpr const char* kRenameDoc =
    "mutation HanabiRenameMetamateSpace($data: XFBUpdateMetamateProjectData!) { "
    "xfb_update_metamate_project(data: $data) { metamate_project { id name } } }";
inline constexpr const char* kEmojiDoc =
    "mutation HanabiSetMetamateSpaceEmoji($data: XFBSetEmojiMetamateProjectData!) { "
    "xfb_set_emoji_metamate_project(data: $data) { metamate_project { id emoji } } }";
inline constexpr const char* kVisibilityDoc =
    "mutation HanabiSetMetamateSpaceVisibility($data: XFBSetVisibilityMetamateProjectData!) { "
    "xfb_set_visibility_metamate_project(data: $data) { metamate_project { id visibility } } }";
inline constexpr const char* kAddMemberDoc =
    "mutation HanabiAddMetamateSpaceMember($data: XFBAddMemberMetamateProjectData!) { "
    "xfb_add_member_metamate_project(data: $data) { metamate_project { id } } }";
inline constexpr const char* kRemoveMemberDoc =
    "mutation HanabiRemoveMetamateSpaceMember($data: XFBRemoveMemberMetamateProjectData!) { "
    "xfb_remove_member_metamate_project(data: $data) { metamate_project { id } } }";
inline constexpr const char* kRoleDoc =
    "mutation HanabiSetMetamateSpaceMemberRole($data: XFBUpsertMetamateProjectMembershipData!) { "
    "xfb_upsert_metamate_project_membership(data: $data) { metamate_project_membership { role } } }";
inline constexpr const char* kArchiveDoc =
    "mutation HanabiArchiveMetamateSpace($data: XFBArchiveMetamateProjectData!) { "
    "xfb_archive_metamate_project(data: $data) { metamate_project { id is_archived } } }";

namespace detail {
inline std::string with(const char* doc, const json& data) {
    return json{{"query_text", doc}, {"variables", json{{"data", data}}.dump()}}.dump();
}
}  // namespace detail

inline Request rename(const std::string& space, const std::string& name) {
    return {detail::with(kRenameDoc, {{"metamate_project_id", space}, {"name", name}, {"surface", kSurface}}),
            "xfb_update_metamate_project"};
}
// An empty icon CLEARS it and travels as null (the field is a nullable String).
inline Request set_emoji(const std::string& space, const std::string& emoji) {
    return {detail::with(kEmojiDoc, {{"metamate_project_id", space},
                                     {"surface", kSurface},
                                     {"emoji", emoji.empty() ? json() : json(emoji)}}),
            "xfb_set_emoji_metamate_project"};
}
inline Request set_visibility(const std::string& space, const std::string& visibility) {
    return {detail::with(kVisibilityDoc, {{"metamate_project_id", space}, {"visibility", visibility}}),
            "xfb_set_visibility_metamate_project"};
}
inline Request add_member(const std::string& space, const std::string& fbid) {
    return {detail::with(kAddMemberDoc, {{"metamate_project_id", space}, {"member", fbid}, {"role", "MEMBER"}}),
            "xfb_add_member_metamate_project"};
}
inline Request remove_member(const std::string& space, const std::string& fbid) {
    return {detail::with(kRemoveMemberDoc, {{"metamate_project_id", space}, {"member", fbid}}),
            "xfb_remove_member_metamate_project"};
}
// NOTE the argument names: this mutation spells them project_id / member_id,
// where every other write here says metamate_project_id / member. Metamate's
// schema, not a typo (the reference pins it in a test; so does this).
inline Request set_role(const std::string& space, const std::string& fbid, bool admin) {
    return {detail::with(kRoleDoc, {{"project_id", space}, {"member_id", fbid}, {"role", admin ? "ADMIN" : "MEMBER"}}),
            "xfb_upsert_metamate_project_membership"};
}
inline Request archive(const std::string& space) {
    return {detail::with(kArchiveDoc, {{"metamate_project_id", space}, {"surface", kSurface}}),
            "xfb_archive_metamate_project"};
}

// What a write's payload (the field's object) says Metamate stored, or nullopt
// when the payload does not show the write happened. Rename / visibility
// answer the stored value; an icon may be stored empty (a clear); archive is
// only true when is_archived comes back TRUE.
inline std::optional<std::string> stored(Write w, const json& payload) {
    if (!payload.is_object()) return std::nullopt;
    if (w == Write::Role) {
        if (!payload.contains("metamate_project_membership") || !payload["metamate_project_membership"].is_object())
            return std::nullopt;
        const json& m = payload["metamate_project_membership"];
        if (!m.contains("role") || !m["role"].is_string() || m["role"].get<std::string>().empty())
            return std::nullopt;
        return m["role"].get<std::string>();
    }
    if (!payload.contains("metamate_project") || !payload["metamate_project"].is_object()) return std::nullopt;
    const json& p = payload["metamate_project"];
    const auto str = [&](const char* k) -> std::optional<std::string> {
        if (!p.contains(k) || !p[k].is_string() || p[k].get<std::string>().empty()) return std::nullopt;
        return p[k].get<std::string>();
    };
    switch (w) {
        case Write::Rename: return str("name");
        case Write::Visibility: return str("visibility");
        case Write::Emoji:
            if (!p.contains("id") || !p["id"].is_string()) return std::nullopt;
            return p.contains("emoji") && p["emoji"].is_string() ? p["emoji"].get<std::string>() : std::string();
        case Write::Archive:
            if (p.contains("is_archived") && p["is_archived"].is_boolean() && p["is_archived"].get<bool>())
                return std::string("archived");
            return std::nullopt;
        case Write::AddMember:
        case Write::RemoveMember:
        case Write::Leave: return std::string();
        case Write::Role: break;
    }
    return std::nullopt;
}

// ---- What a reader is told -------------------------------------------------

inline const char* fallback(Write w) {
    switch (w) {
        case Write::Rename: return "Could not rename this Space.";
        case Write::Emoji: return "Could not change this Space's icon.";
        case Write::Visibility: return "Could not change who can see this Space.";
        case Write::AddMember: return "Could not add that person to this Space.";
        case Write::RemoveMember: return "Could not remove that person from this Space.";
        case Write::Leave: return "Could not leave this Space.";
        case Write::Role: return "Could not change that person's role.";
        case Write::Archive: return "Could not archive this Space.";
    }
    return "That change did not go through.";
}

// The server's message, from Hanabi's failure string: the part in the
// trailing parentheses ("no data.<field> (<message>)"), else the whole string.
inline std::string server_words(const std::string& error) {
    const auto open = error.find(" (");
    if (open != std::string::npos && !error.empty() && error.back() == ')')
        return error.substr(open + 2, error.size() - open - 3);
    return error;
}

namespace detail {
inline std::string lower(std::string_view s) {
    std::string o;
    for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
}  // namespace detail

// The web's own lists (SpaceRefusal): a refusal vs an outage, and InternGraph's
// operator envelope, which a reader must never be shown.
inline bool is_denial(std::string_view message) {
    const std::string l = detail::lower(message);
    for (const char* w : {"permission", "denied", "not allowed", "unauthorized", "privacy", "admin"})
        if (l.find(w) != std::string::npos) return true;
    return false;
}
inline bool is_operator_envelope(std::string_view message) {
    const std::string l = detail::lower(message);
    return l.find("field implementation threw an exception") != std::string::npos ||
           l.find("owned by oncalls") != std::string::npos;
}

// The sentence a failed write shows: the server's own words when they are
// worth showing, else this write's sentence.
inline std::string refusal(Write w, const std::string& error) {
    const std::string words = server_words(error);
    const bool transport = words.rfind("memory HTTP", 0) == 0 || words.rfind("memory unreachable", 0) == 0 ||
                           words == "no success/result envelope" || words == "not JSON" ||
                           words.rfind("no data.", 0) == 0;
    if (words.empty() || is_operator_envelope(words) || transport) return fallback(w);
    return words;
}

// A SENSITIVE Space can never be shown to the whole company: the control locks
// rather than offering a refusal the server is sure to give.
inline bool visibility_locked(const spaces::Space& s) { return s.sensitivity == "SENSITIVE"; }

}  // namespace api::space_manage
