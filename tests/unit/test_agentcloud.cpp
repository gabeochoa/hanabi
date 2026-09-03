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
#include <vector>

#include "../../src/api/agentcloud_auth.h"
#include "../../src/api/agentcloud_client.h"
#include "../../src/ecs/thread_model.h"
#include "../../vendor/nlohmann/json.hpp"

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
using api::agentcloud::classify_live_frame_parsed;
using api::agentcloud::fold_session_renamed;
using api::SessionSummary;
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

static void test_children_fold_into_a_count_and_leave_the_list() {
    // A parent and its three sub-agents, exactly as the server sends them:
    // four rows, the children naming their parent. The sidebar must see ONE
    // row carrying 1/3, not four peers.
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"p","title":"coordinating","last_seq":9,
       "status":{"state":"working"}},
      {"session_id":"c1","title":"shard 1","last_seq":8,"parent":"p",
       "status":{"state":"working"}},
      {"session_id":"c2","title":"shard 2","last_seq":7,"parent":"p",
       "status":{"state":"done"}},
      {"session_id":"c3","title":"shard 3","last_seq":6,"parent":"p",
       "status":{"state":"blocked"}}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].id == "p");
    CHECK(out[0].sub_agent_count == 3);
    // NONE of them is live, and the working one is the interesting case: it
    // CLAIMS to be working and has no run behind it, which the server's own
    // client calls a corpse — "spec 029 measured a 925-session fleet at 132
    // claiming `working` against 3 that really were" (Puffin,
    // HomeSessionList.swift, HomeGroup.of), and buckets as finished rather
    // than running. Counting a claim as a live worker is how "1/3" ends up on
    // a subtree where nothing has moved in a day.
    CHECK(out[0].sub_agent_running_count == 0);
    // The live child is the one the wire says is running, and it still counts.
    const auto live = parse_sessions_reply(R"({"type":"sessions","sessions":[
      {"session_id":"p","title":"coordinating","last_seq":9,
       "status":{"state":"working"},"running":true},
      {"session_id":"c1","title":"shard 1","last_seq":8,"parent":"p",
       "status":{"state":"working"},"running":true},
      {"session_id":"c2","title":"shard 2","last_seq":7,"parent":"p",
       "status":{"state":"done"}}
    ]})");
    CHECK(live.size() == 1);
    CHECK(live[0].sub_agent_count == 2);
    CHECK(live[0].sub_agent_running_count == 1);
}

static void test_a_childless_row_carries_no_count() {
    // Absent parentage must read as zero, not as an unset field the row then
    // draws a "0" for.
    const std::string reply =
        R"({"type":"sessions","sessions":[{"session_id":"a","last_seq":7}]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].sub_agent_count == 0);
    CHECK(out[0].sub_agent_running_count == 0);
}

static void test_an_orphan_child_is_still_not_a_root_row() {
    // The parent is not in this page. The child is still a sub-agent, and
    // listing it as a top-level thread was the noise being removed -- so it
    // is dropped, and its count goes nowhere rather than onto a stranger.
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"root","last_seq":9},
      {"session_id":"orphan","last_seq":8,"parent":"absent"}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(out[0].id == "root");
    CHECK(out[0].sub_agent_count == 0);
}

static void test_status_bag_drives_attention() {    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"blocked","last_seq":4,"status":{"state":"blocked"}},
      {"session_id":"review","last_seq":3,
       "status":{"state":"done","attention":"review"}},
      {"session_id":"done","last_seq":2,"status":{"state":"done"}},
      {"session_id":"working","last_seq":1,"status":{"state":"working"}},
      {"session_id":"live","last_seq":0,"status":{"state":"working"},
       "running":true},
      {"session_id":"dead","last_seq":0,"status":{"state":"failed"}}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 6);
    CHECK(out[0].state == ThreadState::Attention);
    CHECK(out[0].tag == ThreadTag::Blocked);
    // "done" plus an explicit review ask still wants the reader.
    CHECK(out[1].state == ThreadState::Attention);
    CHECK(out[1].tag == ThreadTag::Review);
    CHECK(out[2].state == ThreadState::Ready);
    CHECK(out[2].tag == ThreadTag::Done);
    // A `working` CLAIM with no run behind it is not a run. It keeps the
    // claim — the row still says the agent's last word was "working" — but it
    // is not Running, so nothing draws it a spinner.
    CHECK(out[3].state == ThreadState::Working);
    CHECK(out[4].state == ThreadState::Running);
    // "failed" is the fifth state the bag can carry. It used to match no
    // branch here at all and fell through to the resolved_status fallback,
    // which returned Ready — a dead run offered up as something to review.
    CHECK(out[5].state == ThreadState::Attention);
    CHECK(out[5].tag == ThreadTag::Failed);
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

// The five event classes the transcript could not represent, in the shapes a
// real session emits them (captured from be9a28d7 and 144601bb, 2026-08-26).
static const char* const kEventClassPage = R"({"type":"page","frames":[
  {"seq":3,"created_at_unix_ms":1787710982169,
   "event":{"type":"node_granted","grant_id":"g-1","node_id":"mac-GRQ7Y259H4",
            "granted_by":"be9a28d7"}},
  {"seq":9,"created_at_unix_ms":1787710985000,
   "event":{"type":"node_attached","node_id":"mac-GRQ7Y259H4"}},
  {"seq":14,"created_at_unix_ms":1787710990000,
   "event":{"type":"skill_invoked","name":"meta-cli","source":"platform",
            "content":"# Meta CLI Quick Reference\n…"}},
  {"seq":21,"created_at_unix_ms":1787711000000,
   "event":{"type":"child_spawn_intended","child":"ff0c19ed",
            "prompt":"You are working on hanabi…"}},
  {"seq":22,"created_at_unix_ms":1787711000500,
   "event":{"type":"child_spawned","child":"ff0c19ed",
            "title":"vis: tab strip, reopened","harness":"native"}},
  {"seq":40,"created_at_unix_ms":1787711400000,
   "event":{"type":"child_spawn_settled","intent":21,
            "outcome":{"outcome":"completed"}}},
  {"seq":41,"created_at_unix_ms":1787711401000,
   "event":{"type":"subscription_delivered","subscription":3916,
            "key":{"kind":"child","child":"ff0c19ed","run_seq":80511},
            "body":"child session ff0c19ed settled: completed"}},
  {"seq":50,"created_at_unix_ms":1787711500000,
   "event":{"type":"message_enqueued","message_id":"m-1","to":"96b16ae2",
            "body":"git stash is per-REPOSITORY, not per-worktree."}},
  {"seq":51,"created_at_unix_ms":1787711500500,
   "event":{"type":"message_delivered","intent":50,
            "outcome":{"outcome":"delivered"}}},
  {"seq":60,"created_at_unix_ms":1787711600000,
   "event":{"type":"notice","kind":"refusal","message":"the model declined"}},
  {"seq":61,"created_at_unix_ms":1787711601000,
   "event":{"type":"status_reported","state":"working",
            "headline":"reading the transcript renderer"}},
  {"seq":62,"created_at_unix_ms":1787711602000,
   "event":{"type":"plan_updated","plan":{"revision":1,"steps":[
     {"id":"s1-0","text":"Read the source","status":"in_progress"}]}}},
  {"seq":63,"created_at_unix_ms":1787711603000,
   "event":{"type":"goal_updated","goal":{"objective":"ship the audit",
     "phase":"active","set_by":"user","revision":1}}},
  {"seq":70,"created_at_unix_ms":1787711700000,
   "event":{"type":"node_detached","node_id":"mac-GRQ7Y259H4"}}
]})";

static const api::Message* row_of(const std::vector<api::Message>& rows,
                                  api::EventKind kind, const std::string& label) {
    for (const auto& m : rows)
        if (m.kind == kind && m.subtitle == label) return &m;
    return nullptr;
}

static void test_every_event_class_gets_a_row() {
    // "you are missing thinking and deliveries" / "you are missing subagents"
    // / "you are missing nodes" / "you are missing skills". All one cause:
    // an event that was not one of four ROLES had nowhere to land, so the
    // fold dropped it and the reader saw a gap.
    const auto rows = parse_page_frames(kEventClassPage);

    CHECK(row_of(rows, api::EventKind::Node, "mac-GRQ7Y259H4") != nullptr);
    CHECK(row_of(rows, api::EventKind::Skill, "meta-cli") != nullptr);
    CHECK(row_of(rows, api::EventKind::Notice, "refusal") != nullptr);
    CHECK(row_of(rows, api::EventKind::Status, "working") != nullptr);
    CHECK(row_of(rows, api::EventKind::Plan, "") != nullptr);
    CHECK(row_of(rows, api::EventKind::Goal, "") != nullptr);
    CHECK(row_of(rows, api::EventKind::Delivery, "child") != nullptr);

    // A node row says WHICH node and WHAT happened to it -- three node events
    // that all read "node" would be no better than dropping two of them.
    int node_rows = 0;
    std::string verbs;
    for (const auto& m : rows)
        if (m.kind == api::EventKind::Node) { ++node_rows; verbs += m.text + " "; }
    CHECK(node_rows == 3);
    CHECK(verbs == "granted attached detached ");

    // An event row is not authored by anybody, and must never render as one.
    for (const auto& m : rows)
        if (m.kind != api::EventKind::Text && m.kind != api::EventKind::Thinking &&
            m.kind != api::EventKind::ToolCall)
            CHECK(m.role == Role::System);
}

static void test_a_spawn_row_learns_its_title_and_its_outcome() {
    // Three events, one row: the intent carries the prompt, `child_spawned`
    // the title (the only human-readable name a spawn gets), and the
    // settlement the outcome -- each arriving later than the last.
    const auto rows = parse_page_frames(kEventClassPage);
    const api::Message* spawn =
        row_of(rows, api::EventKind::SubAgent, "vis: tab strip, reopened");
    CHECK(spawn != nullptr);
    if (spawn == nullptr) return;
    CHECK(spawn->tool_status == "completed");
    CHECK(spawn->text == "You are working on hanabi…");
    // One row, not three.
    int spawn_rows = 0;
    for (const auto& m : rows)
        if (m.kind == api::EventKind::SubAgent) ++spawn_rows;
    CHECK(spawn_rows == 1);
}

static void test_an_outbound_message_carries_its_receipt() {
    // message_enqueued is this session speaking to another; the delivery
    // receipt lands later and names the enqueue's seq, exactly as a tool
    // result names its intent.
    const auto rows = parse_page_frames(kEventClassPage);
    const api::Message* sent =
        row_of(rows, api::EventKind::Delivery, "to 96b16ae2");
    CHECK(sent != nullptr);
    if (sent != nullptr) CHECK(sent->tool_status == "delivered");
}

// Every frame below is the shape a real turn puts on the wire, taken from a
// capture of one (session 69167c25, 2026-08-26): a thinking block and a
// tool_use block stream through the SAME `block_delta{append}` frames the
// reply does, and only the `start` says which is which.
static const char* const kRealTurnFrames[] = {
    R"({"type":"frame","frame":"delta","seq":135,"event":{"type":"model_call_started","call":128}})",
    R"({"type":"frame","frame":"delta","seq":136,"event":{"type":"block_delta","run":7,"call":128,"index":0,
        "delta":{"delta":"start","kind":{"kind":"thinking"}}}})",
    R"({"type":"frame","frame":"delta","seq":137,"event":{"type":"block_delta","run":7,"call":128,"index":0,
        "delta":{"delta":"append","text":"I shouldn't reuse any existing nodes"}}})",
    R"({"type":"frame","frame":"durable","seq":152,"event":{"type":"block","run":7,"call":128,"index":0,
        "block":{"kind":"thinking","text":"I shouldn't reuse any existing nodes"}}})",
    R"({"type":"frame","frame":"delta","seq":154,"event":{"type":"block_delta","run":7,"call":128,"index":1,
        "delta":{"delta":"start","kind":{"kind":"tool_use","call_id":"toolu_1","tool":"step"}}}})",
    R"({"type":"frame","frame":"delta","seq":156,"event":{"type":"block_delta","run":7,"call":128,"index":1,
        "delta":{"delta":"append","text":"{\"text\": \"Probing\"}"}}})",
    R"({"type":"frame","frame":"durable","seq":164,"event":{"type":"block","run":7,"call":128,"index":1,
        "block":{"kind":"tool_use","call_id":"toolu_1","tool":"step","input":"{\"text\": \"Probing\"}"}}})",
    R"({"type":"frame","frame":"durable","seq":180,"event":{"type":"tool_intent","call_id":"toolu_1","tool":"step"}})",
    R"({"type":"frame","frame":"delta","seq":190,"event":{"type":"block_delta","run":7,"call":128,"index":2,
        "delta":{"delta":"start","kind":{"kind":"text"}}}})",
    R"({"type":"frame","frame":"delta","seq":191,"event":{"type":"block_delta","run":7,"call":128,"index":2,
        "delta":{"delta":"append","text":"PONG-A1"}}})",
    R"({"type":"frame","frame":"durable","seq":192,"event":{"type":"block","run":7,"call":128,"index":2,
        "block":{"kind":"text","text":"PONG-A1"}}})",
    R"({"type":"frame","frame":"durable","seq":200,"event":{"type":"run_finished","run":7,
        "outcome":{"outcome":"completed"}}})",
};

static void test_a_live_turn_is_only_what_the_agent_said() {
    // A1. Reply text, reasoning and the JSON argument object of a tool call
    // all arrive as block_delta appends carrying nothing but `index` and
    // `text`. Streaming every append as reply text concatenated all three
    // into one bubble: against the real session above, the reply came back
    //     {"text": "Probing"}PONG-A1
    // and in a turn with real reasoning and a dozen tool rounds the answer is
    // a fragment buried in a wall of JSON -- "i cant see your messages to me".
    api::StreamSink sink;
    std::string streamed;
    int thinking_events = 0, tool_events = 0;
    sink.on_delta = [&](const std::string& d) { streamed += d; };
    sink.on_event = [&](const api::StreamEvent& e) {
        if (e.kind == api::StreamEventKind::Thinking) ++thinking_events;
        if (e.kind == api::StreamEventKind::ToolCall) ++tool_events;
    };

    api::agentcloud::LiveTurn turn;
    bool finished = false;
    for (const char* f : kRealTurnFrames) {
        if (!turn.feed(nlohmann::json::parse(f, nullptr, false), sink)) {
            finished = true;
            break;
        }
    }
    CHECK(finished);
    CHECK(turn.assembled().text == "PONG-A1");
    CHECK(streamed == "PONG-A1");
    // Reasoning and the call are still REPORTED -- they are just not the reply.
    CHECK(thinking_events >= 1);
    CHECK(tool_events == 1);
}

static void test_an_unattributed_increment_is_still_shown() {
    // Attaching mid-block means the `start` that named the kind is already
    // past. Dropping the append would lose real reply text, so an increment
    // this build cannot attribute is text -- the same reading the stateless
    // classifier has always taken.
    api::agentcloud::LiveBlocks blocks;
    const std::string app =
        R"({"type":"frame","frame":"delta","event":{"type":"block_delta","index":4,
            "delta":{"delta":"append","text":"orphan"}}})";
    const LF lf = classify_live_frame_parsed(
        nlohmann::json::parse(app, nullptr, false), blocks);
    CHECK(lf.kind == LF::Kind::TextAppend);
    CHECK(lf.payload == "orphan");
}

static void test_block_indices_restart_with_each_model_call() {
    // Indices are per model call. Without the reset, call N+1's index 0 --
    // its reply -- inherits call N's index 0, which is usually reasoning.
    api::agentcloud::LiveBlocks blocks;
    const auto feed = [&](const std::string& s) {
        return classify_live_frame_parsed(nlohmann::json::parse(s, nullptr, false),
                                          blocks);
    };
    feed(R"({"type":"frame","event":{"type":"block_delta","index":0,
             "delta":{"delta":"start","kind":{"kind":"thinking"}}}})");
    CHECK(feed(R"({"type":"frame","event":{"type":"block_delta","index":0,
                  "delta":{"delta":"append","text":"x"}}})")
              .kind == LF::Kind::ThinkingAppend);
    feed(R"({"type":"frame","event":{"type":"model_call_started","call":2}})");
    CHECK(feed(R"({"type":"frame","event":{"type":"block_delta","index":0,
                  "delta":{"delta":"append","text":"x"}}})")
              .kind == LF::Kind::TextAppend);
}

static void test_the_parsed_frame_overload_is_what_the_socket_uses() {
    // The websocket receive loop parses each frame to read its "type", then
    // classifies it. It used to hand the CLASSIFIER a msg.dump() of the object
    // it was already holding, so every frame was parse -> dump -> parse (2.3 us
    // a frame, at token rate). It now passes the object.
    //
    // This test exists because that made the production path a DIFFERENT
    // overload from the one every other test in this file drives. Both must
    // agree on every frame shape, including the malformed ones -- a json
    // overload that quietly disagreed about, say, retract would corrupt live
    // bubbles while the whole string-driven suite above stayed green.
    const std::string frames[] = {
        R"({"type":"frame","frame":"value","seq":1,
            "event":{"type":"block","block":{"kind":"text","text":"hi"}}})",
        R"({"type":"frame","frame":"value","seq":1,
            "event":{"type":"block","block":{"kind":"thinking","text":"hmm"}}})",
        R"({"type":"frame","frame":"durable","seq":2,
            "event":{"type":"tool_intent","tool":"bash","input":"{}"}})",
        R"({"type":"frame","frame":"durable","seq":3,
            "event":{"type":"run_finished","outcome":{"outcome":"completed"}}})",
        R"({"type":"frame","frame":"retract","key":{"x":1},
            "event":{"type":"block","block":{"kind":"text","text":"gone"}}})",
        R"({"type":"frame","frame":"value",
            "event":{"type":"block","block":{"kind":"tool_use","tool":"bash"}}})",
        R"({"type":"frame","frame":"delta","seq":81,
            "event":{"type":"block_delta","index":0,
                     "delta":{"delta":"start","kind":{"kind":"text"}}}})",
        R"({"type":"frame","frame":"delta","seq":82,
            "event":{"type":"block_delta","index":0,
                     "delta":{"delta":"append","text":"\n2\n3"}}})",
        R"({"type":"frame","event":{"type":"block_delta",
            "delta":{"delta":"rewrite","text":"x"}}})",
        R"({"type":"frame","event":{"type":"session_renamed","title":"new"}})",
        R"({"type":"frame"})",
        R"({"type":"frame","event":{"type":"invented_next_quarter"}})",
    };
    for (const std::string& f : frames) {
        const auto viaString = classify_live_frame(f);
        const auto viaObject =
            classify_live_frame_parsed(nlohmann::json::parse(f, nullptr, false));
        CHECK(viaString.kind == viaObject.kind);
        CHECK(viaString.payload == viaObject.payload);
    }

    // The json overload must survive the shapes the string one turns into a
    // discarded parse, because a socket can hand it anything.
    CHECK(classify_live_frame_parsed(nlohmann::json::parse("not json", nullptr, false))
              .kind == LF::Kind::Ignore);
    CHECK(classify_live_frame_parsed(nlohmann::json(nlohmann::json::value_t::null))
              .kind == LF::Kind::Ignore);
    CHECK(classify_live_frame_parsed(nlohmann::json::array()).kind == LF::Kind::Ignore);
}

static void test_rename_echo_folds_into_the_title() {
    // The durable echo is the ONLY thing that renames a session: the modal
    // sends a title, and what the sidebar row and the tab then read is
    // whatever comes back on this frame -- including a title the server
    // normalised into something other than what was asked for.
    SessionSummary s;
    s.id = "abc";
    s.title = "old title";
    const std::string echo =
        R"({"type":"frame","frame":"durable","seq":7,
            "event":{"type":"session_renamed","title":"quarterly numbers"}})";
    CHECK(fold_session_renamed(echo, s));
    CHECK(s.title == "quarterly numbers");
}

static void test_only_a_rename_frame_touches_the_title() {
    SessionSummary s;
    s.title = "untouched";
    const std::string other =
        R"({"type":"frame","frame":"durable","seq":8,
            "event":{"type":"block","block":{"kind":"text","text":"hi"}}})";
    CHECK(!fold_session_renamed(other, s));
    // A rename with no title is not an instruction to blank the row.
    const std::string titleless =
        R"({"type":"frame","event":{"type":"session_renamed"}})";
    CHECK(!fold_session_renamed(titleless, s));
    CHECK(!fold_session_renamed("not json", s));
    CHECK(!fold_session_renamed("", s));
    CHECK(s.title == "untouched");
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

static void test_fork_wire_contract_and_child_catalog() {
    const auto fork =
        nlohmann::json::parse(api::agentcloud::fork_command_json("source-1"));
    CHECK(fork["cmd"] == "fork");
    CHECK(fork["source_session_id"] == "source-1");
    CHECK(!fork.contains("before_seq"));

    const auto btw =
        nlohmann::json::parse(api::agentcloud::fork_with_prompt_command_json(
            "source-1", "why?", "BTW: why?"));
    CHECK(btw["cmd"] == "fork_with_prompt");
    CHECK(btw["source_session_id"] == "source-1");
    CHECK(btw["prompt"] == "why?");
    CHECK(btw["title"] == "BTW: why?");
    CHECK(!btw.contains("input"));

    CHECK(api::agentcloud::parse_created_session_id(
              R"({"type":"created","session":{"session_id":"fork-1"}})") ==
          "fork-1");
    CHECK(api::agentcloud::parse_created_session_id("{}") == "");
    CHECK(api::agentcloud::hello_has_capability(
        R"({"capabilities":["rename_v1","fork_with_prompt_v1"]})",
        "fork_with_prompt_v1"));
    CHECK(!api::agentcloud::hello_has_capability(
        R"({"capabilities":["rename_v1"]})", "fork_with_prompt_v1"));
    CHECK(!api::agentcloud::hello_has_capability("{}", "fork_with_prompt_v1"));

    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"root","last_seq":9},
      {"session_id":"run","title":"active child","last_seq":8,
       "parent":"root","running":true},
      {"session_id":"fail","title":"failed child","last_seq":7,
       "parent":"root","status":{"state":"failed"}},
      {"session_id":"done","title":"done child","last_seq":6,
       "parent":"root","status":{"state":"done"}}
    ]})";
    const auto children = api::agentcloud::parse_subagents_reply(reply, 2);
    CHECK(children.size() == 2);
    if (children.size() == 2) {
        CHECK(children[0].id == "run");
        CHECK(children[0].parent_id == "root");
        CHECK(children[0].state == api::ThreadState::Running);
        CHECK(children[1].id == "fail");
        CHECK(children[1].tag == api::ThreadTag::Failed);
    }
}

// A thread WAITING for an answer and a thread whose run is BLOCKED are two
// different asks, and hanabi used to tag them both Blocked. The split is the
// point of the mark, so it is pinned here rather than only where it is drawn.
static void test_waiting_is_not_blocked() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"waiting","last_seq":2,"status":{"state":"waiting"}},
      {"session_id":"blocked","last_seq":1,"status":{"state":"blocked"}},
      {"session_id":"asked","last_seq":0,
       "status":{"state":"waiting","attention":"review"}}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 3);
    CHECK(out[0].tag == ThreadTag::Waiting);
    CHECK(out[1].tag == ThreadTag::Blocked);
    // An explicit review ask still outranks the plain wait: it says which KIND
    // of answer is owed, which is more than "waiting" does.
    CHECK(out[2].tag == ThreadTag::Review);
    // Both still want the reader.
    CHECK(out[0].state == ThreadState::Attention);
    CHECK(out[1].state == ThreadState::Attention);
}

// The three brakes/stamps the CATALOG ROW carries. They ride the row rather
// than an attach, which is the whole reason the list can mark them.
static void test_row_carries_the_server_brakes() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"ice","last_seq":3,"running":true,
       "frozen":{"by":"root7","reason":"canary owner is reviewing"}},
      {"session_id":"filed","last_seq":1,"archived_at_unix_ms":1781520000000},
      {"session_id":"plain","last_seq":0}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 3);
    CHECK(out[0].frozen);
    CHECK(out[0].frozen_by == "root7");
    CHECK(out[0].frozen_reason == "canary owner is reviewing");
    // A frozen thread can be running at the same time; the freeze does not
    // overwrite what the run is doing, it sits on top of it.
    CHECK(out[0].state == ThreadState::Running);
    CHECK(out[1].server_archived_at_ms == 1781520000000LL);
    // Absent everywhere means absent, not defaulted-on.
    CHECK(!out[2].frozen);
    CHECK(out[2].server_archived_at_ms == 0);
    CHECK(out[2].frozen_by.empty());
}

// The brakes an ATTACH carries. `channel_replies_paused` is a `WireState` key
// with no summary-row equivalent, so this is the only path that can learn it:
// a row-shaped fixture would pass while the real client saw nothing.
static void test_attach_state_carries_the_brakes() {
    api::Session s;
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"channel_replies_paused":true,
            "halted":true,
            "halted_by":{"by":"root9","reason":"parent halted the subtree"}}})",
        s);
    CHECK(s.summary.replies_paused);
    CHECK(s.halted);
    CHECK(s.halt_engaged());
    CHECK(s.halted_by == "root9");
    CHECK(s.halted_reason == "parent halted the subtree");

    // Both are skip-if-false, and both REPLACE: a re-attach after the brakes
    // were lifted sends neither key, and neither may survive it.
    api::agentcloud::parse_session_brakes(R"({"type":"hello","state":{}})", s);
    CHECK(!s.summary.replies_paused);
    CHECK(!s.halted);
    CHECK(!s.halt_engaged());
    CHECK(s.halted_by.empty());
    CHECK(s.halted_reason.empty());

    // Paused alone, which is the ordinary case: a live thread that will not
    // answer is not a halted one.
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"channel_replies_paused":true}})", s);
    CHECK(s.summary.replies_paused);
    CHECK(!s.halted);

    // Explicit false clears, and unreadable input changes nothing.
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"channel_replies_paused":false,
            "halted":false}})",
        s);
    CHECK(!s.summary.replies_paused);
    s.summary.replies_paused = true;
    s.halted = true;
    api::agentcloud::parse_session_brakes("{not json", s);
    CHECK(s.summary.replies_paused);
    CHECK(s.halted);
}

// A DESCENDANT of a halted session: `halted` is its own journal-folded flag
// and stays false, while `halted_by` is the containment the server derived
// over the ancestor chain. The two are separate facts -- the wire's own doc
// calls halted_by "distinct from `halted`" -- so reading the containment only
// when the own flag is set leaves the descendant unbraked, which is a thread
// that will start no run while the composer says everything is fine.
static void test_containment_engages_without_the_own_flag() {
    api::Session s;
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"halted":false,
            "halted_by":{"by":"root9","reason":"the parent halted the whole subtree"}}})",
        s);
    CHECK(!s.halted);
    CHECK(s.halt_contained);
    CHECK(s.halt_engaged());
    CHECK(s.halted_by == "root9");

    // The brake the composer reads engages on it, and warns rather than
    // refusing -- input still queues against the resume.
    const ecs::model::Brake b = ecs::model::brake_for("child1", nullptr, &s);
    CHECK(b.engaged);
    CHECK(!b.refuses_input);
    CHECK(b.caption ==
          "Halted by an ancestor thread \xe2\x80\x94 the parent halted the "
          "whole subtree");

    // `halted_by` is cleared by ABSENCE alone. An own-halt with no containment
    // is the other half of the pair and still engages.
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"halted":true}})", s);
    CHECK(s.halted);
    CHECK(!s.halt_contained);
    CHECK(s.halt_engaged());
    CHECK(s.halted_by.empty());
    CHECK(ecs::model::brake_for("s", nullptr, &s).caption ==
          "Halted \xe2\x80\x94 no run will start until it is resumed");

    // Neither key: nothing engaged.
    api::agentcloud::parse_session_brakes(R"({"type":"hello","state":{}})", s);
    CHECK(!s.halt_engaged());
    CHECK(!ecs::model::brake_for("s", nullptr, &s).engaged);
}

// A freeze FAILS CLOSED: the object's presence is the brake, and `by`/`reason`
// are display metadata. A malformed one that read as unfrozen would let the
// composer take a message nothing will ever answer.
static void test_a_malformed_freeze_still_freezes() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"empty","last_seq":4,"frozen":{}},
      {"session_id":"noby","last_seq":3,"frozen":{"reason":"x"}},
      {"session_id":"full","last_seq":2,
       "frozen":{"by":"root7","reason":"canary owner is reviewing"}},
      {"session_id":"none","last_seq":1},
      {"session_id":"null","last_seq":0,"frozen":null}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 5);
    CHECK(out[0].frozen);
    CHECK(out[0].frozen_by.empty());
    CHECK(out[1].frozen);
    CHECK(out[1].frozen_reason == "x");
    CHECK(out[2].frozen);
    CHECK(out[2].frozen_by == "root7");
    // Absence, and only absence, means unfrozen.
    CHECK(!out[3].frozen);
    CHECK(!out[4].frozen);
    // A freeze with no prose still refuses the composer.
    CHECK(ecs::model::brake_for("empty", &out[0], nullptr).refuses_input);
    CHECK(ecs::model::brake_for("empty", &out[0], nullptr).caption == "Frozen");
    CHECK(ecs::model::status_glyph(out[0]) == ecs::model::StatusGlyph::Frozen);
}

// The attach greeting carries `frozen` too. Without reading it there, a freeze
// that lands after the last catalog poll leaves the composer open until the
// next refresh -- the window in which the reader types into a dead thread.
static void test_attach_state_carries_the_freeze() {
    api::Session s;
    api::agentcloud::parse_session_brakes(
        R"({"type":"hello","state":{"frozen":{"by":"root7","reason":"under review"}}})",
        s);
    CHECK(s.summary.frozen);
    CHECK(s.summary.frozen_by == "root7");
    CHECK(s.summary.frozen_reason == "under review");
    CHECK(ecs::model::brake_for("s", &s.summary, &s).refuses_input);

    // REPLACE, like every other key in the bag: a re-attach after the thaw
    // omits it, and the brake must lift rather than persist.
    api::agentcloud::parse_session_brakes(R"({"type":"hello","state":{}})", s);
    CHECK(!s.summary.frozen);
    CHECK(s.summary.frozen_by.empty());
    CHECK(!ecs::model::brake_for("s", &s.summary, &s).engaged);
}

// The row carries the OTHER two and not this one. A `channel_replies_paused`
// on a summary row is not a thing the server sends, and reading one would be
// a field that is always false pretending to be a feature.
static void test_the_row_does_not_carry_paused() {
    const std::string reply = R"({"type":"sessions","sessions":[
      {"session_id":"quiet","last_seq":2,"channel_replies_paused":true}
    ]})";
    const auto out = parse_sessions_reply(reply);
    CHECK(out.size() == 1);
    CHECK(!out[0].replies_paused);
}

static void test_backward_paging_never_rewinds_the_live_plan() {
    api::Session session;
    api::agentcloud::parse_plan_goal_state(
        R"({"type":"hello","state":{
          "goal":{"objective":"ship the fix","done_when":"all tests pass",
                  "phase":"active","note":"one failure left","set_by":"user",
                  "revision":2},
          "plan":{"title":"Release","revision":4,"steps":[
            {"id":"s4-0","text":"Reproduce","status":"completed","note":"done"},
            {"id":"s4-1","text":"Fix","status":"in_progress"},
            {"id":"s4-2","text":"Verify","status":"pending"},
            {"id":"s4-3","text":"Drop old path","status":"cancelled"},
            {"id":"s4-4","text":"Future state","status":"new_status"}]}}})",
        session);
    CHECK(session.plan.has_value());
    CHECK(session.goal.has_value());
    CHECK(session.plan->title == "Release");
    CHECK(session.plan->steps.size() == 5);
    CHECK(session.plan->steps[0].id == "s4-0");
    CHECK(session.plan->steps[0].note == "done");
    CHECK(session.plan->steps[0].text == "Reproduce");
    CHECK(session.plan->steps[3].status ==
          api::SessionPlanStep::Status::Cancelled);
    CHECK(session.plan->steps[4].status == api::SessionPlanStep::Status::Unknown);
    CHECK(session.plan->completed() == 1);
    CHECK(session.plan->current() != nullptr);
    CHECK(session.plan->current()->text == "Fix");
    CHECK(!session.plan->finished());
    CHECK(session.plan->chip_label() == "Plan 1/5");
    CHECK(session.goal->objective == "ship the fix");
    CHECK(session.goal->done_when == "all tests pass");
    CHECK(session.goal->phase == api::GoalPhase::Active);

    api::Session mismatched;
    api::agentcloud::parse_plan_goal_state(
        R"({"state":{"plan":{"steps":[
          {"step":"Tolerated decoder spelling","status":"completed"}]}}})",
        mismatched);
    CHECK(mismatched.plan.has_value());
    CHECK(mismatched.plan->steps[0].text == "Tolerated decoder spelling");

    api::SessionPlan completed;
    completed.steps = {
        {"a", "One", "", api::SessionPlanStep::Status::Completed},
        {"b", "Two", "", api::SessionPlanStep::Status::Completed},
    };
    CHECK(completed.finished());
    CHECK(completed.chip_label() == "Plan complete");
    completed.steps[1].status = api::SessionPlanStep::Status::Cancelled;
    CHECK(completed.finished());
    CHECK(completed.chip_label() == "Plan cancelled");

    const auto read_phase = [](const std::string& phase) {
        api::Session value;
        api::agentcloud::parse_plan_goal_state(
            "{\"state\":{\"goal\":{\"objective\":\"x\",\"phase\":\"" +
                phase + "\"}}}",
            value);
        return value.goal->phase;
    };
    CHECK(read_phase("active") == api::GoalPhase::Active);
    CHECK(read_phase("paused") == api::GoalPhase::Paused);
    CHECK(read_phase("blocked") == api::GoalPhase::Blocked);
    CHECK(read_phase("completed") == api::GoalPhase::Completed);
    CHECK(read_phase("cleared") == api::GoalPhase::Cleared);
    CHECK(read_phase("future") == api::GoalPhase::Unknown);

    const std::string old_page = R"({"type":"page","frames":[
      {"seq":80,"created_at_unix_ms":1700000000000,
       "event":{"type":"plan_updated","explanation":"Dropped the rollout step",
         "plan":{"revision":1,"steps":[
           {"id":"s1-0","text":"Old plan","status":"completed"}]}}},
      {"seq":81,"created_at_unix_ms":1700000001000,
       "event":{"type":"goal_updated","goal":{"objective":"old goal",
         "phase":"completed","set_by":"user","revision":1}}}]})";
    api::agentcloud::install_paged_transcript(old_page, session);
    CHECK(session.plan->revision == 4);
    CHECK(session.plan->current()->text == "Fix");
    CHECK(session.goal->revision == 2);
    CHECK(session.goal->phase == api::GoalPhase::Active);
    CHECK(session.messages.size() == 2);
    CHECK(session.messages[0].kind == api::EventKind::Plan);
    CHECK(session.messages[0].subtitle.empty());
    CHECK(session.messages[0].text.find("Dropped the rollout step") !=
          std::string::npos);
    CHECK(session.messages[1].kind == api::EventKind::Goal);

    api::agentcloud::parse_plan_goal_state(R"({"state":{}})", session);
    CHECK(!session.plan.has_value());
    CHECK(!session.goal.has_value());
    api::agentcloud::install_paged_transcript(old_page, session);
    CHECK(!session.plan.has_value());
    CHECK(!session.goal.has_value());
    CHECK(session.messages.size() == 2);
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
    test_children_fold_into_a_count_and_leave_the_list();
    test_a_childless_row_carries_no_count();
    test_an_orphan_child_is_still_not_a_root_row();
    test_status_bag_drives_attention();
    test_waiting_is_not_blocked();
    test_row_carries_the_server_brakes();
    test_attach_state_carries_the_brakes();
    test_the_row_does_not_carry_paused();
    test_containment_engages_without_the_own_flag();
    test_a_malformed_freeze_still_freezes();
    test_attach_state_carries_the_freeze();
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
    test_every_event_class_gets_a_row();
    test_a_spawn_row_learns_its_title_and_its_outcome();
    test_an_outbound_message_carries_its_receipt();
    test_a_live_turn_is_only_what_the_agent_said();
    test_an_unattributed_increment_is_still_shown();
    test_block_indices_restart_with_each_model_call();
    test_the_parsed_frame_overload_is_what_the_socket_uses();
    test_settled_block_does_not_reprint_streamed_text();
    test_attach_greeting_carries_budget_and_occupancy();
    test_stale_occupancy_is_reported_not_swallowed();
    test_no_tokens_bag_means_no_meter();
    test_occupancy_without_a_budget_still_counts();
    test_unreadable_greeting_is_empty_not_a_crash();
    test_rename_echo_folds_into_the_title();
    test_only_a_rename_frame_touches_the_title();
    test_fork_wire_contract_and_child_catalog();
    test_backward_paging_never_rewinds_the_live_plan();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
