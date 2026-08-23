// Unit tests for the agentcloud transport slice: query encoding and the
// env-driven config gate. No network — mint_token and the socket are covered
// by running the real thing, not by a test that would need a proxy.
//
// percent_encode earns a test because it fails SILENTLY: the verifier is
// `SERVICE_IDENTITY:<name>`, and an unescaped colon comes back as an HTTP 400
// with an opaque body, which reads like an auth problem rather than a typo.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/api/agentcloud_auth.h"
#include "../../src/api/agentcloud_client.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using api::agentcloud::auth_config_from_env;
using api::agentcloud::percent_encode;
using api::agentcloud::parse_sessions_reply;
using api::ThreadState;
using api::ThreadTag;

static void test_percent_encode_escapes_the_colon() {
    // The one that matters: the verifier's separator.
    CHECK(percent_encode("SERVICE_IDENTITY:a.b.c") ==
          "SERVICE_IDENTITY%3Aa.b.c");
}

static void test_percent_encode_keeps_unreserved() {
    // RFC 3986 unreserved: ALPHA / DIGIT / - . _ ~ pass through untouched.
    CHECK(percent_encode("azAZ09-._~") == "azAZ09-._~");
}

static void test_percent_encode_escapes_the_rest() {
    CHECK(percent_encode("a b") == "a%20b");
    CHECK(percent_encode("a/b") == "a%2Fb");
    CHECK(percent_encode("a&b=c") == "a%26b%3Dc");
    CHECK(percent_encode("") == "");
    // High bytes must not sign-extend into a negative index.
    CHECK(percent_encode("\xC3\xA9") == "%C3%A9");
}

static void test_unconfigured_by_default() {
    unsetenv("HANABI_AC_MINT_HOST");
    unsetenv("HANABI_AC_VERIFIER");
    unsetenv("HANABI_AC_HOST");
    const auto cfg = auth_config_from_env();
    // Nothing set means "run the mock", not "fail at startup" — this is what
    // keeps a machine with no proxy working out of the box.
    CHECK(!cfg.configured());
    CHECK(cfg.proxy_host == "127.0.0.1");
    CHECK(cfg.proxy_port == 10054);
    CHECK(cfg.validity_secs == 10800);
}

static void test_configured_needs_all_three() {
    setenv("HANABI_AC_MINT_HOST", "mint.example", 1);
    CHECK(!auth_config_from_env().configured());
    setenv("HANABI_AC_VERIFIER", "SERVICE_IDENTITY:x", 1);
    CHECK(!auth_config_from_env().configured());
    setenv("HANABI_AC_HOST", "orch.example", 1);
    CHECK(auth_config_from_env().configured());
}

static void test_bad_numbers_keep_the_default() {
    setenv("HANABI_AC_PROXY_PORT", "not-a-port", 1);
    // A typo should not silently dial port 0.
    CHECK(auth_config_from_env().proxy_port == 10054);
    setenv("HANABI_AC_PROXY_PORT", "0", 1);
    CHECK(auth_config_from_env().proxy_port == 10054);
    setenv("HANABI_AC_PROXY_PORT", "9999", 1);
    CHECK(auth_config_from_env().proxy_port == 9999);
    unsetenv("HANABI_AC_PROXY_PORT");
}

static void test_empty_env_reads_as_unset() {
    setenv("HANABI_AC_PROXY_HOST", "", 1);
    // `FOO= ./hanabi` means "leave it alone", not "set it to nothing".
    CHECK(auth_config_from_env().proxy_host == "127.0.0.1");
    unsetenv("HANABI_AC_PROXY_HOST");
}


// --- sessions reply mapping -------------------------------------------------
// Shapes taken verbatim from a live `list` reply (2066 rows), including the
// nulls: 40 of those rows had title:null, which threw on the first live run
// because nlohmann's .value() only falls back for an ABSENT key.

static void test_null_title_falls_back_and_does_not_throw() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"a","title":null,"last_seq":5,
       "status":{"state":"done","subject":"Subs table for M","attention":""}},
      {"session_id":"b","title":null,"last_seq":4}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 2);
    // A null title borrows the model's own subject rather than rendering blank.
    CHECK(out[0].title == "Subs table for M");
    // With nothing to borrow, say so instead of leaving an unclickable row.
    CHECK(out[1].title == "(untitled)");
}

static void test_rows_sort_newest_first_by_seq() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"old","last_seq":1},
      {"session_id":"new","last_seq":900},
      {"session_id":"mid","last_seq":50}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 3);
    CHECK(out[0].id == "new");
    CHECK(out[1].id == "mid");
    CHECK(out[2].id == "old");
}

static void test_no_timestamp_on_this_wire() {
    // Documents a real gap rather than an oversight: the row has last_seq and
    // nothing clock-like, so relative time cannot be rendered from the list.
    const std::string reply =
        R"({"type":"sessions","sessions":[{"session_id":"a","last_seq":7}]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].updated_at == 0);
}

static void test_status_bag_drives_attention() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"blocked","last_seq":4,"status":{"state":"blocked"}},
      {"session_id":"review","last_seq":3,
       "status":{"state":"done","attention":"review"}},
      {"session_id":"done","last_seq":2,"status":{"state":"done"}},
      {"session_id":"working","last_seq":1,"status":{"state":"working"}}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 4);
    CHECK(out[0].state == ThreadState::Attention);
    CHECK(out[0].tag == ThreadTag::Blocked);
    // "done" plus an explicit review ask still wants the reader.
    CHECK(out[1].state == ThreadState::Attention);
    CHECK(out[1].tag == ThreadTag::Review);
    CHECK(out[2].state == ThreadState::Ready);
    CHECK(out[2].tag == ThreadTag::Done);
    CHECK(out[3].state == ThreadState::Running);
}

static void test_falls_back_to_coarse_status_without_the_bag() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"run","last_seq":3,"running":true},
      {"session_id":"idle","last_seq":2,"running":false,
       "resolved_status":{"kind":"idle"}},
      {"session_id":"ok","last_seq":1,"running":false,
       "resolved_status":{"kind":"outcome"},"last_outcome":{"outcome":"completed"}}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 3);
    CHECK(out[0].state == ThreadState::Running);
    CHECK(out[1].state == ThreadState::Parked);
    CHECK(out[2].state == ThreadState::Ready);
    CHECK(out[2].tag == ThreadTag::Done);
}

static void test_unreadable_input_is_empty_not_a_crash() {
    CHECK(parse_sessions_reply("").empty());
    CHECK(parse_sessions_reply("not json").empty());
    CHECK(parse_sessions_reply(R"({"type":"sessions"})").empty());
    CHECK(parse_sessions_reply(R"({"type":"sessions","sessions":"nope"})").empty());
    // A row with no id could never be opened, so it is dropped, not rendered.
    CHECK(parse_sessions_reply(
              R"({"type":"sessions","sessions":[{"title":"x"}]})").empty());
}

static void test_workspace_becomes_the_folder() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"a","last_seq":1,"workspace":"/home/me/gdrive"}]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].folder == "/home/me/gdrive");
}

static void test_per_session_scratch_workspace_is_not_a_folder() {
    // 2054 of 2066 live rows had a server-made scratch dir named after the
    // session. Grouping on those verbatim gave ~2055 folders of one session
    // each, which is a sidebar nobody can use. A path carrying its own session
    // id is machine-generated, not a place a person picked.
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"0051e820-bf8c","last_seq":2,
       "workspace":"/tmp/agentcloud-101-workspace/0051e820-bf8c"},
      {"session_id":"b","last_seq":1,"workspace":"/tmp"}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 2);
    CHECK(out[0].folder == "");      // scratch: no folder at all
    CHECK(out[1].folder == "/tmp");  // a real directory still groups
}

int main() {
    std::printf("== test_agentcloud (transport config, encoding, session mapping) ==\n");
    test_percent_encode_escapes_the_colon();
    test_percent_encode_keeps_unreserved();
    test_percent_encode_escapes_the_rest();
    test_unconfigured_by_default();
    test_configured_needs_all_three();
    test_bad_numbers_keep_the_default();
    test_empty_env_reads_as_unset();
    test_null_title_falls_back_and_does_not_throw();
    test_rows_sort_newest_first_by_seq();
    test_no_timestamp_on_this_wire();
    test_status_bag_drives_attention();
    test_falls_back_to_coarse_status_without_the_bag();
    test_unreadable_input_is_empty_not_a_crash();
    test_workspace_becomes_the_folder();
    test_per_session_scratch_workspace_is_not_a_folder();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
