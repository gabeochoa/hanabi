#include <cstdio>
#include <string>

#include "../../src/api/companion_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace co = api::companion;
using json = nlohmann::json;

static json diff_data() {
    return json{{"phabricator_diff_query",
                 json::array({{{"results",
                                {{"nodes",
                                  json::array({{{"number", 123},
                                                {"diff_title", "Fix the thing"},
                                                {"commit_message", "Summary"},
                                                {"file_count", 2},
                                                {"repository", {{"name", "fbsource"}}},
                                                {"status_badge", {{"label", "Needs Review"}}},
                                                {"latest_active_visible_phabricator_version",
                                                 {{"id", "v99"},
                                                  {"number", "5"},
                                                  {"ordinal_label", {{"abbreviated", "V2"}}},
                                                  {"phabricator_version_changesets",
                                                   {{"nodes",
                                                     json::array({{{"filename", "a.cpp"},
                                                                   {"source_file_change_type", "TYPE_CHANGE"},
                                                                   {"add_lines", 3},
                                                                   {"delete_lines", 1}},
                                                                  {{"filename", "b.h"},
                                                                   {"source_file_change_type", "TYPE_ADD"},
                                                                   {"add_lines", 9}}})}}}}}}})}}}}})}};
}

static void test_diff_document_and_files() {
    auto r = co::parse_diff(diff_data(), "123");
    CHECK(std::holds_alternative<co::Diff>(r));
    if (auto* d = std::get_if<co::Diff>(&r)) {
        CHECK(d->title == "Fix the thing" && d->status == "Needs Review" && d->repo == "fbsource");
        CHECK(d->versionId == "v99" && d->versionLabel == "V2");
        CHECK(d->files.size() == 2 && d->files[0].added == 3 && d->files[1].change == "TYPE_ADD");
    }
    CHECK(std::holds_alternative<std::string>(co::parse_diff(diff_data(), "124")));
    const json vars = json::parse(json::parse(co::diff_body(123))["variables"].get<std::string>());
    CHECK(vars["queryParams"][0]["numbers"][0] == 123 && vars["files"] == 200);
}

static void test_every_hunk_of_the_file_asked() {
    const co::File f{"a.cpp", "", "TYPE_CHANGE", 3, 1};
    const json data{{"phabricator_version",
                     {{"id", "v99"},
                      {"phabricator_version_changesets",
                       {{"nodes", json::array({{{"filename", "a.cpp"},
                                                {"comparison_hunks",
                                                 json::array({{{"old_contents", "a\n"}, {"new_contents", "b\n"},
                                                               {"new_offset", 3}, {"new_length", 1}},
                                                              {{"old_contents", "x\n"}, {"new_contents", "y\nz\n"},
                                                               {"new_offset", 40}, {"new_length", 2}}})}}})}}}}}};
    auto r = co::parse_file(data, "v99", f);
    CHECK(std::holds_alternative<std::vector<co::Hunk>>(r));
    if (auto* h = std::get_if<std::vector<co::Hunk>>(&r)) {
        CHECK(h->size() == 2 && (*h)[1].newOffset == 40 && (*h)[1].newText == "y\nz\n");
    }
    CHECK(std::holds_alternative<std::string>(co::parse_file(data, "v98", f)));
    CHECK(std::holds_alternative<std::string>(co::parse_file(data, "v99", co::File{"other.cpp", "", "TYPE_CHANGE", 0, 0})));
}

static void test_task_and_its_comments() {
    const json task{{"task",
                     {{"task_number", 456},
                      {"task_title", "Ship it"},
                      {"task_priority", "high"},
                      {"is_closed", false},
                      {"task_progress_status", "In Progress"},
                      {"task_description", {{"text", "do the thing"}}},
                      {"task_owner", {{"name", "Owner Person"}, {"unixname", "owner"}}},
                      {"created_time", 1700000000},
                      {"tags", {{"count", 1}, {"nodes", json::array({{{"name", "hanabi"}}})}}}}}};
    auto r = co::parse_task(task, "456");
    CHECK(std::holds_alternative<co::Task>(r));
    if (auto* k = std::get_if<co::Task>(&r)) {
        CHECK(k->title == "Ship it" && k->status == "In Progress" && k->owner == "Owner Person");
        CHECK(k->tags.size() == 1 && k->created == 1700000000);
    }
    const json comments{{"task",
                         {{"task_number", 456},
                          {"intern_activity_comments",
                           {{"count", 2},
                            {"nodes", json::array({{{"id", "c2"}, {"created_time", 1700000100},
                                                    {"activity_actor", {{"full_name", "Second"}}},
                                                    {"rte_content", {{"rte_content_plain_text", "newest"}}}},
                                                   {{"id", "c1"}, {"activity_actor", {{"full_name", "First"}}},
                                                    {"rte_content", {{"rte_content_plain_text", "first"}}}}})}}}}}};
    auto c = co::parse_comments(comments, "456");
    CHECK(std::holds_alternative<std::vector<co::Comment>>(c));
    if (auto* v = std::get_if<std::vector<co::Comment>>(&c))
        CHECK(v->size() == 2 && (*v)[0].author == "Second" && (*v)[0].text == "newest");
    CHECK(std::holds_alternative<std::string>(co::parse_comments(comments, "457")));
}

// The web's 9/21 task panel: subscribers (employees named, others counted),
// people with their handles, and the Related read (kt-pv24).
static void test_task_people_and_related() {
    using namespace api::companion;
    json data = {{"task",
                  {{"task_number", 555}, {"task_title", "T"}, {"task_progress_status", "IN_PROGRESS"},
                   {"task_owner", {{"name", "Pat Owner"}, {"unixname", "pat"}}},
                   {"subscribers",
                    {{"count", 3},
                     {"nodes", json::array({{{"__typename", "Employee"}, {"name", "Pat Owner"}, {"unixname", "pat"}},
                                            {{"__typename", "Group"}, {"name", "oncall"}}})}}}}}};
    auto p = parse_task(data, "555");
    auto* t = std::get_if<Task>(&p);
    CHECK(t != nullptr);
    if (t) {
        CHECK(t->status == "In progress");
        CHECK(person_label(t->owner, t->ownerHandle) == "Pat Owner (pat)");
        CHECK(t->subscriberCount == 3 && t->subscribers.size() == 1 && t->subscribers[0] == "Pat Owner (pat)");
    }
    CHECK(person_label("pat", "pat") == "pat" && person_label("", "pat") == "pat");
    json rel = {{"task",
                 {{"task_number", 555},
                  {"task_phabricator_diffs",
                   {{"count", 4}, {"nodes", json::array({{{"number", 1234}, {"diff_title", "D"}, {"status_badge", {{"label", "Accepted"}}}},
                                                         {{"number", "bad"}, {"diff_title", "x"}}})}}},
                  {"subtasks", {{"count", 1}, {"nodes", json::array({{{"task_number", 9}, {"task_title", "S"}, {"is_closed", true}}})}}},
                  {"subtask_parent", {{"task_number", 7}, {"task_title", "P"}, {"task_progress_status", "NO_PROGRESS"}}}}}};
    auto r = parse_related(rel, "555");
    auto* v = std::get_if<Related>(&r);
    CHECK(v != nullptr);
    if (v) {
        CHECK(v->diffs.rows.size() == 1 && v->diffs.total == 4 && v->diffs.rows[0].status == "Accepted" &&
              v->diffs.rows[0].kind == 'D');
        CHECK(v->subtasks.rows.size() == 1 && v->subtasks.rows[0].status == "Closed");
        CHECK(v->parent && v->parent->number == "7" && v->parent->status == "Open");
        CHECK(v->blocks.rows.empty() && !v->empty());
    }
    // An answer for another task is not believed.
    CHECK(std::holds_alternative<std::string>(parse_related(rel, "556")));
}

int main() {
    test_task_people_and_related();
    test_diff_document_and_files();
    test_every_hunk_of_the_file_asked();
    test_task_and_its_comments();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
