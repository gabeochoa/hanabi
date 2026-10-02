#pragma once

// The Companion's reads (the reference's CompanionDiff / CompanionTask):
// a Phabricator diff and a task, over the agentcloud web app's /api/graphql
// with the same credential as memory -- the queries are the reference's own.
//
//   diff:     title, status, repo, the latest visible version, its files
//   file:     ONE file's comparison hunks, all of them, drawn as one unified
//             diff (the reference's 0.8.9: every hunk, not one hunk's two
//             sides behind a toggle)
//   task:     title, status, priority, owner, description, tags
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
    "query HanabiCompanionTask($number: Int!, $tags: Int!) { task(number: $number) { "
    "task_number task_title task_priority task_progress_status is_closed created_time "
    "updated_time task_description { text } task_owner { name unixname } task_creator { name "
    "unixname } tags(first: $tags) { count nodes { name } } } }";
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
    return body(kTaskDoc, json{{"number", number}, {"tags", 20}});
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

inline std::string task_status(const json& t) {
    if (t.value("is_closed", false)) return "Closed";
    const std::string s = str(t, "task_progress_status");
    return s.empty() ? "Open" : s;
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
    if (owner.is_object()) k.owner = str(owner, "name").empty() ? str(owner, "unixname") : str(owner, "name");
    const json& creator = t.value("task_creator", json());
    if (creator.is_object())
        k.creator = str(creator, "name").empty() ? str(creator, "unixname") : str(creator, "name");
    k.created = seconds(t.value("created_time", json()));
    k.updated = seconds(t.value("updated_time", json()));
    const json& tags = t.value("tags", json::object());
    if (tags.contains("nodes") && tags["nodes"].is_array())
        for (const json& n : tags["nodes"])
            if (!str(n, "name").empty()) k.tags.push_back(str(n, "name"));
    return k;
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
