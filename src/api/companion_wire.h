#pragma once

// The Companion's reads (the reference's CompanionDiff / CompanionTask):
// a Phabricator diff and a task, over the agentcloud web app's /api/graphql
// with the same credential as memory -- the queries are the reference's own.
//
//   diff:     title, status, repo, the latest visible version, its files
//   file:     ONE file's comparison hunks, all of them, drawn as one unified
//             diff (the reference's 0.8.9: every hunk, not one hunk's two
//             sides behind a toggle)
//   task:     title, status, priority, owner and creator (name and handle),
//             description, tags, subscribers (the web's 9/21 task panel)
//   related:  linked diffs with their status, the tasks it depends on and
//             blocks, subtasks and parent -- its own read, like comments,
//             so a miss fails that page and never the card (kt-pv24)
//   comments: the newest ten, author, time, plain text (the reference's
//             0.8.9 Comments page), in their own query so a schema or
//             permission miss fails that page and never the card
//
// Every answer is checked against what was asked (the number, the version,
// the path) before it is believed. Pure.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::companion {

using json = nlohmann::json;

inline constexpr int kMaxFiles = 200;
inline constexpr int kMaxComments = 10;
inline constexpr int kMaxTags = 20;
inline constexpr int kMaxSubscribers = 24;
inline constexpr int kMaxRelated = 12;
inline constexpr std::size_t kProseLimit = 64000;

inline constexpr const char* kDiffDoc =
    "query HanabiCompanionDiff($queryParams: [PhabricatorDiffQueryParams!]!, $files: Int!) { "
    "phabricator_diff_query(query_params: $queryParams) { results { nodes { number diff_title "
    "commit_message file_count repository { name } status_badge { "
    "label(should_show_land_job_status: true) } latest_active_visible_phabricator_version { id "
    "number ordinal_label { abbreviated } phabricator_version_changesets(first: $files) { nodes "
    "{ filename old_file add_lines delete_lines source_file_change_type } } } } } } }";
inline constexpr const char* kFileDoc =
    "query HanabiCompanionDiffFile($version: ID!, $paths: [String!]!) { "
    "phabricator_version(fbid: $version) { id number phabricator_version_changesets(first: 1, "
    "files: $paths) { nodes { filename old_file source_file_change_type comparison_hunks { "
    "old_contents new_contents new_offset new_length } } } } }";
inline constexpr const char* kTaskDoc =
    "query HanabiCompanionTask($number: Int!, $tags: Int!, $subscribers: Int!) { task(number: "
    "$number) { task_number task_title task_priority task_progress_status is_closed created_time "
    "updated_time task_description { text } task_owner { name unixname } task_creator { name "
    "unixname } tags(first: $tags) { count nodes { name } } subscribers(first: $subscribers) { "
    "count nodes { __typename ... on Employee { name unixname } } } } }";
inline constexpr const char* kRelatedDoc =
    "query HanabiCompanionTaskRelated($number: Int!, $rows: Int!) { task(number: $number) { "
    "task_number task_phabricator_diffs(first: $rows) { count nodes { number diff_title "
    "status_badge { label(should_show_land_job_status: true) } } } task_parents(first: $rows) { "
    "count nodes { task_number task_title task_progress_status is_closed } } task_children(first: "
    "$rows) { count nodes { task_number task_title task_progress_status is_closed } } "
    "subtasks(first: $rows) { count nodes { task_number task_title task_progress_status is_closed "
    "} } subtask_parent { task_number task_title task_progress_status is_closed } } }";
inline constexpr const char* kCommentsDoc =
    "query HanabiCompanionTaskComments($number: Int!, $rows: Int!) { task(number: $number) { "
    "task_number intern_activity_comments(first: $rows, exclude_deleted: true) { count nodes { "
    "id created_time activity_actor { full_name } rte_content { rte_content_plain_text } } } } }";

// The viewer's Sensitive-mode rollout gate, the same lookup the web client
// makes before create (the reference's SensitiveModeGate). Advisory: the
// orchestrator stays the admission authority.
inline constexpr const char* kSensitiveGateDoc =
    "query HanabiSensitiveModeGate { viewer { sensitive_mode: "
    "if_gk(gk: \"agentcloud_sensitive_mode_switch\") { __typename } } }";

inline bool sensitive_gate_admits(const json& data) {
    if (!data.is_object() || !data.contains("viewer") || !data["viewer"].is_object()) return false;
    const json& v = data["viewer"];
    return v.contains("sensitive_mode") && v["sensitive_mode"].is_object() &&
           v["sensitive_mode"].value("__typename", std::string()) == "Viewer";
}

inline std::string body(const char* doc, const json& vars) {
    return json{{"query_text", doc}, {"variables", vars.dump()}}.dump();
}

inline std::string sensitive_gate_body() { return body(kSensitiveGateDoc, json::object()); }

inline std::string diff_body(std::int64_t number) {
    return body(kDiffDoc, json{{"queryParams", json::array({{{"numbers", {number}}}})},
                               {"files", kMaxFiles}});
}
inline std::string file_body(const std::string& versionId, const std::string& path) {
    return body(kFileDoc, json{{"version", versionId}, {"paths", {path}}});
}
inline std::string task_body(std::int64_t number) {
    return body(kTaskDoc, json{{"number", number}, {"tags", kMaxTags}, {"subscribers", kMaxSubscribers}});
}
inline std::string related_body(std::int64_t number) {
    return body(kRelatedDoc, json{{"number", number}, {"rows", kMaxRelated}});
}
inline std::string comments_body(std::int64_t number) {
    return body(kCommentsDoc, json{{"number", number}, {"rows", kMaxComments}});
}

// A number as the graph spells it: an int or a digit string.
inline std::string digits(const json& v) {
    if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
    if (v.is_string()) return v.get<std::string>();
    return {};
}

inline std::string str(const json& o, const char* k) {
    if (!o.is_object() || !o.contains(k) || !o[k].is_string()) return {};
    return o[k].get<std::string>();
}

struct File {
    std::string path;
    std::string oldPath;
    std::string change;  // TYPE_ADD / TYPE_CHANGE / TYPE_DELETE / ...
    int added = 0;
    int deleted = 0;
};

struct Diff {
    std::string number;
    std::string title;
    std::string status;
    std::string repo;
    std::string message;
    std::string versionId;
    std::string versionLabel;
    std::vector<File> files;
    int totalFiles = 0;
};

struct Hunk {
    std::string oldText;
    std::string newText;
    int newOffset = 0;
    int newLength = 0;
};

struct Task {
    std::string number;
    std::string title;
    std::string status;
    std::string priority;
    std::string description;
    std::string owner;
    std::string creator;
    std::int64_t created = 0;
    std::int64_t updated = 0;
    std::vector<std::string> tags;
    int tagCount = 0;
    // People as the web's panel names them: name, and the handle beside it.
    std::string ownerHandle;
    std::string creatorHandle;
    std::vector<std::string> subscribers;  // "Name (handle)", employees only
    int subscriberCount = -1;              // the server's total; -1 = not read
};

// One row of a task's related work: a diff or a task, its title and status.
struct RelatedRow {
    char kind = 'T';  // 'D' a diff, 'T' a task
    std::string number;
    std::string title;
    std::string status;
};
struct RelatedList {
    std::vector<RelatedRow> rows;
    int total = 0;
};
struct Related {
    RelatedList diffs, dependsOn, blocks, subtasks;
    std::optional<RelatedRow> parent;
    [[nodiscard]] bool empty() const {
        return diffs.rows.empty() && dependsOn.rows.empty() && blocks.rows.empty() &&
               subtasks.rows.empty() && !parent;
    }
};

struct Comment {
    std::string id;
    std::string author;
    std::int64_t created = 0;
    std::string text;
};

template <class T>
using Parsed = std::variant<T, std::string>;  // the value, or the reader's sentence

inline Parsed<Diff> parse_diff(const json& data, const std::string& number) {
    const auto fail = [](const char* s) { return Parsed<Diff>(std::string(s)); };
    if (!data.contains("phabricator_diff_query") || !data["phabricator_diff_query"].is_array() ||
        data["phabricator_diff_query"].empty())
        return fail("Couldn't read the diff response. Try reloading.");
    const json& results = data["phabricator_diff_query"][0].value("results", json::object());
    if (!results.contains("nodes") || !results["nodes"].is_array())
        return fail("Couldn't read the diff response. Try reloading.");
    const json* node = nullptr;
    for (const json& n : results["nodes"])
        if (digits(n.value("number", json())) == number) node = &n;
    if (node == nullptr) return fail("This diff was not found, or you do not have access to it.");
    const json& v = node->value("latest_active_visible_phabricator_version", json());
    if (!v.is_object() || str(v, "id").empty())
        return fail("This diff has no readable version. Open it in your browser.");
    Diff d;
    d.number = number;
    d.title = str(*node, "diff_title");
    d.message = str(*node, "commit_message");
    if (d.message.size() > kProseLimit) d.message.resize(kProseLimit);
    d.status = str(node->value("status_badge", json::object()), "label");
    d.repo = str(node->value("repository", json::object()), "name");
    d.versionId = str(v, "id");
    d.versionLabel = str(v.value("ordinal_label", json::object()), "abbreviated");
    const json& cs = v.value("phabricator_version_changesets", json::object());
    if (!cs.contains("nodes") || !cs["nodes"].is_array() ||
        cs["nodes"].size() > static_cast<std::size_t>(kMaxFiles))
        return fail("Couldn't read the file list. Try reloading.");
    for (const json& f : cs["nodes"]) {
        File file;
        file.path = str(f, "filename");
        file.change = str(f, "source_file_change_type");
        if (file.path.empty() || file.change.empty())
            return fail("Couldn't read the file list. Try reloading.");
        file.oldPath = str(f, "old_file");
        file.added = f.value("add_lines", 0);
        file.deleted = f.value("delete_lines", 0);
        d.files.push_back(std::move(file));
    }
    d.totalFiles = std::max<int>(static_cast<int>(d.files.size()), node->value("file_count", 0));
    return d;
}

// Every comparison hunk of the one file asked for, in order.
inline Parsed<std::vector<Hunk>> parse_file(const json& data, const std::string& versionId,
                                            const File& expected) {
    using R = Parsed<std::vector<Hunk>>;
    const json& v = data.value("phabricator_version", json());
    if (!v.is_object() || str(v, "id") != versionId)
        return R(std::string("The file response does not match this diff version. Reload the diff."));
    const json& cs = v.value("phabricator_version_changesets", json::object());
    if (!cs.contains("nodes") || !cs["nodes"].is_array() || cs["nodes"].size() != 1 ||
        str(cs["nodes"][0], "filename") != expected.path)
        return R(std::string("The file response does not match this diff version. Reload the diff."));
    const json& hunks = cs["nodes"][0].value("comparison_hunks", json());
    if (!hunks.is_array() || hunks.empty())
        return R(std::string("This file has no text preview. Open it in your browser to read it."));
    std::vector<Hunk> out;
    for (const json& h : hunks) {
        Hunk k;
        k.oldText = expected.change == "TYPE_ADD" ? "" : str(h, "old_contents");
        k.newText = expected.change == "TYPE_DELETE" ? "" : str(h, "new_contents");
        k.newOffset = h.value("new_offset", 0);
        k.newLength = h.value("new_length", 0);
        out.push_back(std::move(k));
    }
    return out;
}

// The status words the reference shows for the graph's progress enum; an
// unknown value is shown as the server spelled it.
inline std::string task_status(const json& t) {
    if (t.value("is_closed", false)) return "Closed";
    const std::string s = str(t, "task_progress_status");
    if (s.empty() || s == "NO_PROGRESS") return "Open";
    if (s == "IN_PROGRESS") return "In progress";
    if (s == "BLOCKED") return "Blocked";
    if (s == "PLANNED") return "Planned";
    if (s == "BACKLOG") return "Backlog";
    if (s == "CLOSED") return "Closed";
    return s;
}

inline std::int64_t seconds(const json& v) {
    if (v.is_number()) return static_cast<std::int64_t>(v.get<double>());
    if (v.is_string()) return std::atoll(v.get<std::string>().c_str());
    return 0;
}

inline Parsed<Task> parse_task(const json& data, const std::string& number) {
    const json& t = data.value("task", json());
    if (!t.is_object()) return Parsed<Task>(std::string("This task was not found, or you do not have access to it."));
    if (digits(t.value("task_number", json())) != number || !t.contains("task_title"))
        return Parsed<Task>(std::string("Couldn't read the task response. Try reloading."));
    Task k;
    k.number = number;
    k.title = str(t, "task_title");
    k.status = task_status(t);
    k.priority = str(t, "task_priority");
    k.description = str(t.value("task_description", json::object()), "text");
    if (k.description.size() > kProseLimit) k.description.resize(kProseLimit);
    const json& owner = t.value("task_owner", json());
    if (owner.is_object()) {
        k.owner = str(owner, "name").empty() ? str(owner, "unixname") : str(owner, "name");
        k.ownerHandle = str(owner, "unixname");
    }
    const json& creator = t.value("task_creator", json());
    if (creator.is_object()) {
        k.creator = str(creator, "name").empty() ? str(creator, "unixname") : str(creator, "name");
        k.creatorHandle = str(creator, "unixname");
    }
    const json& subs = t.value("subscribers", json());
    if (subs.is_object() && subs.contains("nodes") && subs["nodes"].is_array()) {
        k.subscriberCount = subs.contains("count") && subs["count"].is_number_integer()
                                ? subs["count"].get<int>()
                                : static_cast<int>(subs["nodes"].size());
        for (const json& n : subs["nodes"]) {
            // Employees only (a group or a bot carries no handle to name).
            if (str(n, "__typename") != "Employee" || str(n, "unixname").empty()) continue;
            if (static_cast<int>(k.subscribers.size()) >= kMaxSubscribers) break;
            const std::string name = str(n, "name").empty() ? str(n, "unixname") : str(n, "name");
            k.subscribers.push_back(name + " (" + str(n, "unixname") + ")");
        }
    }
    k.created = seconds(t.value("created_time", json()));
    k.updated = seconds(t.value("updated_time", json()));
    const json& tags = t.value("tags", json::object());
    if (tags.contains("nodes") && tags["nodes"].is_array())
        for (const json& n : tags["nodes"])
            if (!str(n, "name").empty()) k.tags.push_back(str(n, "name"));
    k.tagCount = tags.contains("count") && tags["count"].is_number_integer() ? tags["count"].get<int>()
                                                                             : static_cast<int>(k.tags.size());
    return k;
}

// A person as the panel draws them: "Pat Owner (pat)", or the name alone.
inline std::string person_label(const std::string& name, const std::string& handle) {
    if (name.empty()) return handle;
    if (handle.empty() || handle == name) return name;
    return name + " (" + handle + ")";
}

namespace detail {
inline std::optional<RelatedRow> related_row(const json& n, bool diff) {
    RelatedRow r;
    r.kind = diff ? 'D' : 'T';
    r.number = digits(n.value(diff ? "number" : "task_number", json()));
    r.title = str(n, diff ? "diff_title" : "task_title");
    if (r.number.empty() || r.title.empty() ||
        r.number.find_first_not_of("0123456789") != std::string::npos)
        return std::nullopt;
    if (diff) {
        r.status = str(n.value("status_badge", json::object()), "label");
        if (r.status.empty()) r.status = "Status unavailable";
    } else {
        r.status = task_status(n);
    }
    return r;
}
inline std::optional<RelatedList> related_list(const json& t, const char* key, bool diff) {
    const json& c = t.value(key, json());
    if (!c.is_object() || !c.contains("nodes") || !c["nodes"].is_array() ||
        c["nodes"].size() > static_cast<std::size_t>(kMaxRelated))
        return std::nullopt;
    RelatedList out;
    for (const json& n : c["nodes"])
        if (auto r = related_row(n, diff)) out.rows.push_back(std::move(*r));
    out.total = c.contains("count") && c["count"].is_number_integer() ? c["count"].get<int>()
                                                                      : static_cast<int>(out.rows.size());
    if (out.total < static_cast<int>(out.rows.size())) return std::nullopt;
    return out;
}
}  // namespace detail

inline Parsed<Related> parse_related(const json& data, const std::string& number) {
    using R = Parsed<Related>;
    const json& t = data.value("task", json());
    if (!t.is_object() || digits(t.value("task_number", json())) != number)
        return R(std::string("Couldn't read this task's related work. Try reloading."));
    Related out;
    const auto take = [&](const char* key, bool diff, RelatedList& into) {
        if (auto l = detail::related_list(t, key, diff)) into = std::move(*l);
    };
    take("task_phabricator_diffs", true, out.diffs);
    take("task_parents", false, out.dependsOn);
    take("task_children", false, out.blocks);
    take("subtasks", false, out.subtasks);
    const json& parent = t.value("subtask_parent", json());
    if (parent.is_object()) out.parent = detail::related_row(parent, false);
    return out;
}

inline Parsed<std::vector<Comment>> parse_comments(const json& data, const std::string& number) {
    using R = Parsed<std::vector<Comment>>;
    const json& t = data.value("task", json());
    if (!t.is_object() || digits(t.value("task_number", json())) != number)
        return R(std::string("Couldn't read this task's comments. Try reloading."));
    const json& c = t.value("intern_activity_comments", json());
    if (!c.is_object() || !c.contains("nodes") || !c["nodes"].is_array() ||
        c["nodes"].size() > static_cast<std::size_t>(kMaxComments))
        return R(std::string("This task's comments are unavailable."));
    std::vector<Comment> out;
    for (const json& n : c["nodes"]) {
        Comment m;
        m.id = str(n, "id");
        if (m.id.empty()) continue;
        m.author = str(n.value("activity_actor", json::object()), "full_name");
        if (m.author.empty()) m.author = "Unknown";
        m.created = seconds(n.value("created_time", json()));
        m.text = str(n.value("rte_content", json::object()), "rte_content_plain_text");
        if (m.text.size() > kProseLimit) m.text.resize(kProseLimit);
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace api::companion
