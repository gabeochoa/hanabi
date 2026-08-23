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
using api::agentcloud::parse_page_frames;
using api::Role;
using api::agentcloud::classify_live_frame;
using api::agentcloud::delta_from_accumulated;
using LF = api::agentcloud::LiveFrame;
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


// --- the keyed fold ---------------------------------------------------------
// Shapes copied from a live `page` reply. The wire carries EVENTS, not
// messages: a tool row is assembled from up to three frames arriving apart.

static void test_user_and_assistant_text_become_messages() {
    const std::string reply = R"({"type":"page","done":true,"frames":[
      {"seq":1,"created_at_unix_ms":1700000000000,
       "event":{"type":"user_input","text":"hello"}},
      {"seq":2,"created_at_unix_ms":1700000002000,
       "event":{"type":"block","block":{"kind":"text","text":"hi back"}}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 2);
    CHECK(out[0].role == Role::User);
    CHECK(out[0].text == "hello");
    // Milliseconds on the wire, seconds in Message.
    CHECK(out[0].created_at == 1700000000);
    CHECK(out[1].role == Role::Assistant);
    CHECK(out[1].text == "hi back");
}

static void test_thinking_is_marked_not_presented_as_speech() {
    const std::string reply = R"({"type":"page","frames":[
      {"seq":1,"event":{"type":"block","block":{"kind":"thinking","text":"hmm"}}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].subtitle == "thinking");
}

static void test_tool_result_folds_back_into_its_intent() {
    // The result names the intent's SEQ and arrives later; it must complete
    // that row rather than append one of its own.
    const std::string reply = R"({"type":"page","frames":[
      {"seq":10,"created_at_unix_ms":1700000000000,
       "event":{"type":"tool_intent","tool":"bash",
                "input":"{\"command\":\"ls -l\"}"}},
      {"seq":11,"event":{"type":"tool_node_selected","intent":10,
                         "node_id":"host.example"}},
      {"seq":12,"created_at_unix_ms":1700000003000,
       "event":{"type":"tool_result","intent":10,
                "outcome":{"outcome":"success",
                           "content":{"text":"total 0"}}}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 1);              // three frames, ONE row
    CHECK(out[0].role == Role::Tool);
    CHECK(out[0].subtitle == "bash");
    // The command, not {"command":"ls -l"}.
    CHECK(out[0].text == "ls -l");
    CHECK(out[0].tool_status == "completed");
    CHECK(out[0].tool_result == "total 0");
    CHECK(out[0].tool_node == "host.example");
    CHECK(out[0].tool_duration_ms == 3000);
}

static void test_failed_tool_reports_failed() {
    const std::string reply = R"({"type":"page","frames":[
      {"seq":10,"event":{"type":"tool_intent","tool":"bash","input":"{}"}},
      {"seq":11,"event":{"type":"tool_result","intent":10,
                         "outcome":{"outcome":"error"}}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].tool_status == "failed");
}

static void test_result_for_an_offpage_intent_is_dropped() {
    // Paging backwards splits turns: a result whose intent is older than this
    // page has no row to complete, and must not invent one.
    const std::string reply = R"({"type":"page","frames":[
      {"seq":11,"event":{"type":"tool_result","intent":999,
                         "outcome":{"outcome":"success"}}}
    ]})";
    CHECK(parse_page_frames(reply).empty());
}

static void test_tool_use_block_does_not_double_the_row() {
    // The model ANNOUNCES a call as a block and the call itself arrives as
    // tool_intent with the same call_id. Rendering both doubles every tool row.
    const std::string reply = R"({"type":"page","frames":[
      {"seq":1,"event":{"type":"block","block":{"kind":"tool_use",
        "call_id":"c1","tool":"bash","input":"{}"}}},
      {"seq":2,"event":{"type":"tool_intent","call_id":"c1","tool":"bash",
        "input":"{\"command\":\"true\"}"}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].role == Role::Tool);
}

static void test_unknown_events_fold_as_nothing() {
    // The server says the vocabulary grows. A new variant must not throw and
    // must not render as a blank row.
    const std::string reply = R"({"type":"page","frames":[
      {"seq":1,"event":{"type":"run_started"}},
      {"seq":2,"event":{"type":"model_call_settled"}},
      {"seq":3,"event":{"type":"something_invented_next_quarter","x":1}},
      {"seq":4,"event":{"type":"user_input","text":"still here"}}
    ]})";
    const auto out = parse_page_frames(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].text == "still here");
}

static void test_bad_page_input_is_empty_not_a_crash() {
    CHECK(parse_page_frames("").empty());
    CHECK(parse_page_frames("not json").empty());
    CHECK(parse_page_frames(R"({"type":"page"})").empty());
    CHECK(parse_page_frames(R"({"type":"page","frames":{}})").empty());
    // Empty text must not become an empty bubble.
    CHECK(parse_page_frames(
              R"({"type":"page","frames":[{"seq":1,"event":{"type":"user_input","text":""}}]})")
              .empty());
}


// --- streaming a live turn --------------------------------------------------

static void test_accumulated_text_becomes_an_increment() {
    // The server sends the WHOLE text at a key each time, not the new part.
    CHECK(delta_from_accumulated("", "Hel") == "Hel");
    CHECK(delta_from_accumulated("Hel", "Hello") == "lo");
    CHECK(delta_from_accumulated("Hello", "Hello") == "");
}

static void test_a_new_block_is_emitted_whole_not_diffed() {
    // A payload that does not extend what we have is a DIFFERENT block, not a
    // rewind. Emitting a suffix of unrelated text would corrupt the bubble.
    CHECK(delta_from_accumulated("Hello there", "Goodbye") == "Goodbye");
    // Shorter but not a prefix -- still a different block.
    CHECK(delta_from_accumulated("Hello", "Hi") == "Hi");
}

static void test_live_text_and_thinking_are_told_apart() {
    const std::string text =
        R"({"type":"frame","frame":"value","seq":1,
            "event":{"type":"block","block":{"kind":"text","text":"hi"}}})";
    CHECK(classify_live_frame(text).kind == LF::Kind::Text);
    CHECK(classify_live_frame(text).payload == "hi");

    const std::string think =
        R"({"type":"frame","frame":"value","seq":1,
            "event":{"type":"block","block":{"kind":"thinking","text":"hmm"}}})";
    CHECK(classify_live_frame(think).kind == LF::Kind::Thinking);
}

static void test_live_tool_call_and_finish() {
    const std::string tool =
        R"({"type":"frame","frame":"durable","seq":2,
            "event":{"type":"tool_intent","tool":"bash","input":"{}"}})";
    CHECK(classify_live_frame(tool).kind == LF::Kind::ToolCall);
    CHECK(classify_live_frame(tool).payload == "bash");

    const std::string fin =
        R"({"type":"frame","frame":"durable","seq":3,
            "event":{"type":"run_finished","outcome":{"outcome":"completed"}}})";
    CHECK(classify_live_frame(fin).kind == LF::Kind::Finished);
}

static void test_retract_and_tool_use_show_nothing() {
    // A retract says a live partial is gone -- there is nothing to render.
    const std::string retract =
        R"({"type":"frame","frame":"retract","key":{"x":1},
            "event":{"type":"block","block":{"kind":"text","text":"gone"}}})";
    CHECK(classify_live_frame(retract).kind == LF::Kind::Ignore);
    // tool_use duplicates the tool_intent that follows it.
    const std::string use =
        R"({"type":"frame","frame":"value",
            "event":{"type":"block","block":{"kind":"tool_use","tool":"bash"}}})";
    CHECK(classify_live_frame(use).kind == LF::Kind::Ignore);
}

static void test_unknown_live_frames_are_ignored_not_fatal() {
    CHECK(classify_live_frame("").kind == LF::Kind::Ignore);
    CHECK(classify_live_frame("not json").kind == LF::Kind::Ignore);
    CHECK(classify_live_frame(R"({"type":"frame"})").kind == LF::Kind::Ignore);
    CHECK(classify_live_frame(
              R"({"type":"frame","event":{"type":"invented_next_quarter"}})")
              .kind == LF::Kind::Ignore);
}


static void test_block_delta_append_is_a_true_increment() {
    // Learned from real traffic, not the docs: block_delta carries the NEW
    // text, while a settled `block` carries the whole thing. Treating an
    // append as accumulated (or the reverse) prints the reply twice.
    const std::string start =
        R"({"type":"frame","frame":"delta","seq":81,
            "event":{"type":"block_delta","index":0,
                     "delta":{"delta":"start","kind":{"kind":"text"}}}})";
    CHECK(classify_live_frame(start).kind == LF::Kind::BlockStart);

    const std::string app =
        R"({"type":"frame","frame":"delta","seq":82,
            "event":{"type":"block_delta","index":0,
                     "delta":{"delta":"append","text":"\n2\n3"}}})";
    CHECK(classify_live_frame(app).kind == LF::Kind::TextAppend);
    CHECK(classify_live_frame(app).payload == "\n2\n3");

    // An unknown delta shape must not be guessed at.
    const std::string weird =
        R"({"type":"frame","event":{"type":"block_delta",
            "delta":{"delta":"rewrite","text":"x"}}})";
    CHECK(classify_live_frame(weird).kind == LF::Kind::Ignore);
}

static void test_settled_block_does_not_reprint_streamed_text() {
    // The end of a turn: appends streamed "1\n2\n3", then the durable block
    // arrives carrying the same text whole. The diff must be empty, or the
    // bubble shows the reply twice.
    const std::string streamed = "1\n2\n3";
    CHECK(delta_from_accumulated(streamed, "1\n2\n3") == "");
    // ...but a tail the appends missed still gets through.
    CHECK(delta_from_accumulated(streamed, "1\n2\n3\n4") == "\n4");
}

// --- the context meter's numbers --------------------------------------------
// Shape copied from a live attach greeting. The denominator decision lives
// here: budget, never window.

static void test_attach_greeting_carries_budget_and_occupancy() {
    const std::string hello = R"({"type":"hello","state":{"title":"t",
      "tokens":{"context":{"budget":800000,"window":1000000},
                "occupancy":{"tokens":258937,"basis":"settled","stale":0,
                             "anchor_seq":1329894}}}})";
    const auto u = api::agentcloud::parse_context_usage(hello);
    CHECK(u.used_tokens == 258937);
    // 800k, not the 1M window: the budget is what triggers compaction.
    CHECK(u.budget_tokens == 800000);
    CHECK(!u.stale);
    CHECK(u.counted());
    CHECK(u.has_denominator());
}

static void test_stale_occupancy_is_reported_not_swallowed() {
    const std::string as_int = R"({"state":{"tokens":{
      "context":{"budget":100},"occupancy":{"tokens":5,"stale":1}}}})";
    CHECK(api::agentcloud::parse_context_usage(as_int).stale);
    // The wire says 0/1 today; a server that switches to a real boolean must
    // not silently start reading as fresh.
    const std::string as_bool = R"({"state":{"tokens":{
      "context":{"budget":100},"occupancy":{"tokens":5,"stale":true}}}})";
    CHECK(api::agentcloud::parse_context_usage(as_bool).stale);
}

static void test_no_tokens_bag_means_no_meter() {
    const std::string hello = R"({"type":"hello","state":{"title":"t"}})";
    const auto u = api::agentcloud::parse_context_usage(hello);
    CHECK(!u.counted());
    CHECK(!u.has_denominator());
    CHECK(!u.stale);
}

static void test_occupancy_without_a_budget_still_counts() {
    // A count with no budget is a real figure and must survive; it just draws
    // no bar.
    const std::string hello =
        R"({"state":{"tokens":{"occupancy":{"tokens":4200}}}})";
    const auto u = api::agentcloud::parse_context_usage(hello);
    CHECK(u.counted());
    CHECK(u.used_tokens == 4200);
    CHECK(!u.has_denominator());
}

static void test_unreadable_greeting_is_empty_not_a_crash() {
    CHECK(!api::agentcloud::parse_context_usage("{not json").counted());
    CHECK(!api::agentcloud::parse_context_usage("").has_denominator());
    const std::string nulls =
        R"({"state":{"tokens":{"context":{"budget":null},
                               "occupancy":{"tokens":null,"stale":null}}}})";
    const auto u = api::agentcloud::parse_context_usage(nulls);
    CHECK(!u.counted());
    CHECK(!u.has_denominator());
    CHECK(!u.stale);
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
    test_user_and_assistant_text_become_messages();
    test_thinking_is_marked_not_presented_as_speech();
    test_tool_result_folds_back_into_its_intent();
    test_failed_tool_reports_failed();
    test_result_for_an_offpage_intent_is_dropped();
    test_tool_use_block_does_not_double_the_row();
    test_unknown_events_fold_as_nothing();
    test_bad_page_input_is_empty_not_a_crash();
    test_accumulated_text_becomes_an_increment();
    test_a_new_block_is_emitted_whole_not_diffed();
    test_live_text_and_thinking_are_told_apart();
    test_live_tool_call_and_finish();
    test_retract_and_tool_use_show_nothing();
    test_unknown_live_frames_are_ignored_not_fatal();
    test_block_delta_append_is_a_true_increment();
    test_settled_block_does_not_reprint_streamed_text();
    test_attach_greeting_carries_budget_and_occupancy();
    test_stale_occupancy_is_reported_not_swallowed();
    test_no_tokens_bag_means_no_meter();
    test_occupancy_without_a_budget_still_counts();
    test_unreadable_greeting_is_empty_not_a_crash();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
