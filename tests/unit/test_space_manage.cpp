#include <cstdio>
#include <string>

#include "../../src/api/space_manage.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sm = api::space_manage;
using json = nlohmann::json;

static json vars_of(const std::string& body) {
    const json b = json::parse(body);
    return json::parse(b["variables"].get<std::string>());
}

int main() {
    // Roster: admins (ADMIN and OWNER alike) first, then by name; a member
    // with no Employee arm is kept but carries no fbid; the count is Metamate's.
    const json roster = json::parse(R"({"xfb_metamate_project":{"team_members":{"count":140,"edges":[
        {"role":"MEMBER","node":{"id":"n3","name":"zed","fbid":"300"}},
        {"role":"OWNER","node":{"id":"n2","name":"Yan","fbid":"200"}},
        {"role":"MEMBER","node":{"id":"n4","name":"Amy"}},
        {"role":"ADMIN","node":{"id":"n1","name":"Bo","fbid":"x1"}},
        {"role":"MEMBER","node":{"name":"no id"}}]}}})");
    const auto r = sm::parse_roster(roster);
    CHECK(r && r->total == 140 && r->members.size() == 4);
    CHECK(r && r->members[0].name == "Bo" && r->members[1].name == "Yan" && r->members[2].name == "Amy" &&
          r->members[3].name == "zed");
    CHECK(r && r->members[0].fbid.empty() && r->members[2].fbid.empty() && r->members[1].fbid == "200");
    CHECK(!sm::parse_roster(json{{"xfb_metamate_project", nullptr}}));
    CHECK(vars_of(sm::roster_body("1593993452358360"))["spaceId"] == "1593993452358360");

    // Viewer fbid, validated.
    CHECK(sm::parse_viewer({{"viewer_intern_user", {{"intern_user", {{"unencoded_id", "1015795541"}}}}}}) ==
          "1015795541");
    CHECK(sm::parse_viewer({{"viewer_intern_user", {{"intern_user", {{"unencoded_id", "abc"}}}}}}).empty());

    // People search: worth it from two characters; a short last token closed.
    CHECK(!sm::worth_searching(" a ") && sm::worth_searching("dj"));
    CHECK(sm::normalize_query("DJ") == "DJ " && sm::normalize_query("Gabe Och") == "Gabe Och" &&
          sm::normalize_query("Gabe O") == "Gabe O " && sm::normalize_query("x ") == "x ");
    const json pv = vars_of(sm::people_body("DJ"));
    CHECK(pv["query"] == "DJ " && pv["types"][0] == "INTERN_USER" && pv["filter"] == "EMPLOYEE_ONLY" &&
          pv["first"] == 6);
    const auto people = sm::parse_people(json::parse(R"({"intern_typeahead_query":{"results":{"nodes":[
        {"fbid":"100","title":"DJ Petersen","subtitle":"Infra"},{"fbid":"bad","title":"Nope"},{"fbid":"101","title":""}]}}})"));
    CHECK(people && people->size() == 1 && (*people)[0].name == "DJ Petersen" && (*people)[0].subtitle == "Infra");

    // Writes: variables, the role mutation's own argument names, a cleared icon is null.
    CHECK(vars_of(sm::rename("1", "New").body)["data"]["name"] == "New");
    CHECK(vars_of(sm::rename("1", "New").body)["data"]["surface"] == "agentcloud");
    CHECK(vars_of(sm::set_emoji("1", "").body)["data"]["emoji"].is_null());
    const json role = vars_of(sm::set_role("1", "2", true).body)["data"];
    CHECK(role["project_id"] == "1" && role["member_id"] == "2" && role["role"] == "ADMIN" &&
          !role.contains("metamate_project_id"));
    CHECK(vars_of(sm::add_member("1", "2").body)["data"]["role"] == "MEMBER");
    CHECK(std::string(sm::archive("1").field) == "xfb_archive_metamate_project");

    // What was stored: never what was typed.
    CHECK(sm::stored(sm::Write::Rename, {{"metamate_project", {{"id", "1"}, {"name", "Stored"}}}}) == "Stored");
    CHECK(!sm::stored(sm::Write::Rename, {{"metamate_project", {{"id", "1"}}}}));
    CHECK(sm::stored(sm::Write::Emoji, {{"metamate_project", {{"id", "1"}, {"emoji", nullptr}}}}) == "");
    CHECK(!sm::stored(sm::Write::Archive, {{"metamate_project", {{"id", "1"}, {"is_archived", false}}}}));
    CHECK(sm::stored(sm::Write::Archive, {{"metamate_project", {{"id", "1"}, {"is_archived", true}}}}));
    CHECK(sm::stored(sm::Write::Role, {{"metamate_project_membership", {{"role", "ADMIN"}}}}) == "ADMIN");

    // Refusals: the server's own words, unless they are operator text or transport.
    CHECK(sm::refusal(sm::Write::Rename, "no data.xfb_update_metamate_project (Only admins can rename)") ==
          "Only admins can rename");
    CHECK(sm::is_denial("Only admins can rename") && !sm::is_denial("timeout"));
    CHECK(sm::refusal(sm::Write::Rename,
                      "no data.x (Field implementation threw an exception. Owned by oncalls: metamate)") ==
          "Could not rename this Space.");
    CHECK(sm::refusal(sm::Write::Leave, "memory HTTP 503") == "Could not leave this Space.");
    // Photos: the variables, the uri, and only https fbcdn hosts are fetched.
    const json phv = vars_of(sm::photo_body("100"));
    CHECK(phv["id"] == "100" && phv["size"] == 40);
    CHECK(sm::parse_photo_uri({{"profile_picture", {{"uri", "https://scontent-sjc3-1.xx.fbcdn.net/v/p.jpg?oh=1&oe=2"}}}}) ==
          "https://scontent-sjc3-1.xx.fbcdn.net/v/p.jpg?oh=1&oe=2");
    CHECK(sm::parse_photo_uri({{"profile_picture", nullptr}}).empty());
    std::string host, path;
    CHECK(sm::photo_url_ok("https://scontent-sjc3-1.xx.fbcdn.net/v/p.jpg?oh=1", &host, &path) &&
          host == "scontent-sjc3-1.xx.fbcdn.net" && path == "/v/p.jpg?oh=1");
    CHECK(!sm::photo_url_ok("http://scontent.xx.fbcdn.net/p.jpg", nullptr, nullptr));
    CHECK(!sm::photo_url_ok("https://evil.example.com/p.jpg", nullptr, nullptr));
    CHECK(!sm::photo_url_ok("https://fbcdn.net.evil.com/p.jpg", nullptr, nullptr));
    CHECK(!sm::photo_url_ok("https://user@scontent.xx.fbcdn.net/p.jpg", nullptr, nullptr));
    CHECK(sm::initial_of("  ana lopez") == "A" && sm::initial_of("") == "?");
    api::spaces::Space s;
    s.sensitivity = "SENSITIVE";
    CHECK(sm::visibility_locked(s));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
