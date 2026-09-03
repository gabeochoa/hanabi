#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/elicitation.h"
#include "../../vendor/nlohmann/json.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using api::AskAction;
using api::AskAnswer;
using api::AskControl;
using api::AskKind;
using api::AskQuestion;
using api::PendingAsk;
using nlohmann::json;
namespace el = api::elicitation;

static const api::AskQuestion* find_q(const std::vector<AskQuestion>& qs,
                                      const std::string& key) {
    for (const auto& q : qs)
        if (q.key == key) return &q;
    return nullptr;
}

static void test_shapes() {
    std::printf("schema shapes\n");
    const std::string one_of = R"({"properties":{"auth":{"title":"Auth method",
        "oneOf":[{"const":"OAuth","title":"OAuth"},{"const":"CAT"}]}}})";
    auto qs = el::parse_schema(one_of, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].key == "auth");
    CHECK(qs[0].prompt == "Auth method");
    CHECK(qs[0].control == AskControl::Single);
    CHECK(qs[0].options.size() == 2);
    CHECK(qs[0].options[1].value == "CAT");
    CHECK(qs[0].options[1].label == "CAT");

    const std::string items_any_of = R"({"properties":{"areas":{
        "items":{"anyOf":[{"const":"A"},{"const":"B"}]}}}})";
    qs = el::parse_schema(items_any_of, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Multi);

    const std::string array_one_of = R"({"properties":{"areas":{
        "type":"array","oneOf":[{"const":"A"},{"const":"B"}]}}})";
    qs = el::parse_schema(array_one_of, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Multi);

    const std::string legacy = R"({"properties":{"pick":{
        "enum":["a","b"],"enumNames":["Alpha"]}}})";
    qs = el::parse_schema(legacy, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Single);
    CHECK(qs[0].options.size() == 2);
    CHECK(qs[0].options[0].label == "Alpha");
    CHECK(qs[0].options[1].label == "b");

    const std::string legacy_array = R"({"properties":{"pick":{
        "type":"array","items":{"enum":["a","b"]}}}})";
    qs = el::parse_schema(legacy_array, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Multi);

    const std::string plain = R"({"properties":{"why":{"type":"string",
        "title":"Why?"}}})";
    qs = el::parse_schema(plain, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Text);
    CHECK(qs[0].prompt == "Why?");

    const std::string empty_one_of =
        R"({"properties":{"pick":{"oneOf":[]}}})";
    qs = el::parse_schema(empty_one_of, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].control == AskControl::Text);

    CHECK(el::parse_schema("", {}).empty());
    CHECK(el::parse_schema("not json", {}).empty());
    CHECK(el::parse_schema(R"({"type":"object"})", {}).empty());

    const std::string no_title = R"({"properties":{"raw":{"type":"string"}}})";
    qs = el::parse_schema(no_title, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].prompt == "raw");
}

static void test_ordering_and_options() {
    std::printf("ordering and options\n");
    const std::string many = R"({"properties":{"q10":{"type":"string"},
        "q2":{"type":"string"},"q1":{"type":"string"}}})";
    const auto qs = el::parse_schema(many, {});
    CHECK(qs.size() == 3);
    CHECK(qs[0].key == "q1");
    CHECK(qs[1].key == "q2");
    CHECK(qs[2].key == "q10");

    const std::string folded = R"({"properties":{"p":{"oneOf":[
        {"const":"fast","title":"fast \u2014 skips the slow checks"},
        {"const":"safe","title":"Safe and slow"},
        {"title":"no const at all"}]}}})";
    const auto opts = el::parse_schema(folded, {})[0].options;
    CHECK(opts.size() == 2);
    CHECK(opts[0].value == "fast");
    CHECK(opts[0].label == "fast");
    CHECK(opts[0].detail == "skips the slow checks");
    CHECK(opts[1].value == "safe");
    CHECK(opts[1].label == "Safe and slow");
    CHECK(opts[1].detail.empty());
}

static void test_companion_and_files() {
    std::printf("companions and file keys\n");
    const std::string with_other = R"({"properties":{
        "auth":{"oneOf":[{"const":"OAuth"}]},
        "auth_other":{"type":"string","title":"Something else"}}})";
    auto qs = el::parse_schema(with_other, {});
    CHECK(qs.size() == 1);
    CHECK(qs[0].key == "auth");
    CHECK(qs[0].free_text_key == "auth_other");
    CHECK(qs[0].free_text_label == "Something else");

    const std::string default_label = R"({"properties":{
        "auth":{"oneOf":[{"const":"OAuth"}]},
        "auth_other":{"type":"string"}}})";
    qs = el::parse_schema(default_label, {});
    CHECK(qs[0].free_text_label == "Other");

    const std::string two_strings = R"({"properties":{
        "data":{"type":"string"},"data_other":{"type":"string"}}})";
    qs = el::parse_schema(two_strings, {});
    CHECK(qs.size() == 2);
    CHECK(qs[0].free_text_key.empty());

    const std::string file_shaped = R"({"properties":{
        "doc":{"type":"string"},"pick":{"oneOf":[{"const":"a"}]}}})";
    qs = el::parse_schema(file_shaped, {"doc", "pick"});
    CHECK(find_q(qs, "doc")->control == AskControl::File);
    CHECK(find_q(qs, "pick")->control == AskControl::Single);
}

static void test_content() {
    std::printf("answer content\n");
    const std::string schema = R"({"properties":{
        "q1":{"oneOf":[{"const":"OAuth"},{"const":"CAT"}]},
        "q1_other":{"type":"string"},
        "q2":{"type":"array","items":{"anyOf":[{"const":"A"},{"const":"B"}]}},
        "q3":{"type":"string"},
        "q4":{"type":"string"}}})";
    PendingAsk ask;
    ask.seq = 12;
    ask.questions = el::parse_schema(schema, {});
    CHECK(ask.questions.size() == 4);

    AskAnswer a;
    a.picks["q1"] = {"OAuth"};
    a.picks["q2"] = {"B", "A"};
    a.text["q3"] = "  because  ";
    a.text["q4"] = "   ";
    const json content = el::content_object(ask, a);
    CHECK(content.contains("q1") && content["q1"] == "OAuth");
    CHECK(content["q2"].is_array());
    CHECK(content["q2"][0] == "A");
    CHECK(content["q2"][1] == "B");
    CHECK(content["q3"] == "because");
    CHECK(!content.contains("q4"));
    CHECK(!content.contains("q1_other"));

    a.text["q1_other"] = "something else";
    CHECK(el::content_object(ask, a)["q1_other"] == "something else");

    AskAnswer none;
    CHECK(el::content_object(ask, none).empty());
    CHECK(!el::answer_has_content(ask, none));
    AskAnswer one;
    one.text["q3"] = "x";
    CHECK(el::answer_has_content(ask, one));

    PendingAsk with_file;
    with_file.questions = el::parse_schema(
        R"({"properties":{"doc":{"type":"string"}}})", {"doc"});
    AskAnswer typed;
    typed.text["doc"] = "cannot go this way";
    CHECK(el::content_object(with_file, typed).empty());
}

static void test_commands() {
    std::printf("resolve commands\n");
    PendingAsk ask;
    ask.seq = 12;
    ask.questions =
        el::parse_schema(R"({"properties":{"q1":{"type":"string"}}})", {});
    AskAnswer a;
    a.text["q1"] = "yes";

    json cmd = json::parse(
        el::resolve_command_json(ask, AskAction::Accept, a));
    CHECK(cmd["cmd"] == "resolve_elicitation");
    CHECK(cmd["elicitation"] == 12);
    CHECK(cmd["action"] == "accept");
    CHECK(cmd["content"]["q1"] == "yes");
    CHECK(!cmd.contains("session"));

    cmd = json::parse(el::resolve_command_json(ask, AskAction::Decline, a));
    CHECK(cmd["action"] == "decline");
    CHECK(!cmd.contains("content"));

    cmd = json::parse(el::resolve_command_json(ask, AskAction::Cancel, a));
    CHECK(cmd["action"] == "cancel");

    PendingAsk child = ask;
    child.child_session = "child-1";
    cmd = json::parse(el::resolve_command_json(child, AskAction::Accept, a));
    CHECK(cmd["session"] == "child-1");

    PendingAsk approval;
    approval.seq = 3;
    approval.kind = AskKind::Approval;
    cmd = json::parse(
        el::resolve_command_json(approval, AskAction::Accept, a));
    CHECK(cmd["action"] == "accept");
    CHECK(!cmd.contains("content"));
    CHECK(el::answer_has_content(approval, AskAnswer{}));

    CHECK(ask.id() == "/#12");
    CHECK(child.id() == "/child-1#12");
    CHECK(ask.id() != child.id());
    PendingAsk other = ask;
    other.owner_session = "session-b";
    PendingAsk mine = ask;
    mine.owner_session = "session-a";
    CHECK(mine.id() != other.id());
    CHECK(mine.id() == "session-a/#12");
}

static void test_state_decode() {
    std::printf("attach state decode\n");
    const json state = json::parse(R"({
      "pending_elicitations":[
        {"elicitation":9,"tool":"ask","message":"second",
         "requested_schema":"{\"properties\":{\"a\":{\"type\":\"string\"}}}"},
        {"elicitation":4,"tool":"shell","message":"first","kind":"approval",
         "input":"{\"command\":\"rm\"}","timeout_ms":120000}],
      "child_pending_elicitations":[
        {"session":"kid","elicitation":{"elicitation":4,"message":"child ask",
         "requested_schema":"{\"properties\":{\"b\":{\"type\":\"string\"}}}"}}]
    })");
    const auto asks = el::asks_from_state(state, "own");
    CHECK(asks.size() == 3);
    CHECK(asks[0].seq == 4);
    CHECK(asks[0].kind == AskKind::Approval);
    CHECK(asks[0].input == "{\"command\":\"rm\"}");
    CHECK(asks[0].timeout_ms == 120000);
    CHECK(asks[0].child_session.empty());
    CHECK(asks[1].seq == 9);
    CHECK(asks[1].kind == AskKind::Form);
    CHECK(asks[1].questions.size() == 1);
    CHECK(asks[2].child_session == "kid");
    CHECK(asks[2].seq == 4);
    CHECK(asks[2].id() == "own/kid#4");
    CHECK(asks[0].id() == "own/#4");
    CHECK(asks[0].answering_session() == "own");
    CHECK(asks[2].answering_session() == "kid");
    CHECK(asks[0].id() != asks[2].id());

    CHECK(el::asks_from_state(json::object(), "own").empty());

    const json unreadable = json::parse(R"({"pending_elicitations":[
        {"elicitation":1,"requested_schema":"{not json"}]})");
    const auto broken = el::asks_from_state(unreadable, "own");
    CHECK(broken.size() == 1);
    CHECK(broken[0].schema_unreadable);
    CHECK(broken[0].questions.empty());

    const json approval_only = json::parse(R"({"pending_elicitations":[
        {"elicitation":1,"kind":"approval"}]})");
    CHECK(!el::asks_from_state(approval_only, "own")[0].schema_unreadable);
}

static void test_settlement() {
    std::printf("settlement frames\n");
    const std::string resolved = R"({"type":"frame","seq":13,"event":{
        "type":"elicitation_resolved","elicitation":12,"action":"accept",
        "content":"{\"q1\":\"CAT\",\"q2\":[\"A\",\"B\"]}","by":{"by":"user"}}})";
    std::uint64_t seq = 0;
    std::string action;
    std::string by;
    CHECK(el::fold_ask_resolved(resolved, &seq, &action, &by));
    CHECK(seq == 12);
    CHECK(action == "accept");
    CHECK(by == "user");

    const std::string timed_out = R"({"event":{
        "type":"elicitation_resolved","elicitation":12,"action":"cancel",
        "by":{"by":"timeout"}}})";
    CHECK(el::fold_ask_resolved(timed_out, &seq, &action, &by));
    CHECK(action == "cancel");
    CHECK(by == "timeout");

    const std::string no_action = R"({"event":{
        "type":"elicitation_resolved","elicitation":7}})";
    CHECK(el::fold_ask_resolved(no_action, &seq, &action, &by));
    CHECK(action == "cancel");

    CHECK(!el::fold_ask_resolved(R"({"event":{"type":"block"}})", &seq,
                                 &action, &by));
    CHECK(!el::fold_ask_resolved("not json", &seq, &action, &by));

    const std::string retract = R"({"event":{
        "type":"child_elicitation_update","session":"kid","elicitation":4,
        "cause":9}})";
    CHECK(el::fold_child_ask_retracted(retract, "kid", 4));
    CHECK(!el::fold_child_ask_retracted(retract, "other", 4));
    CHECK(!el::fold_child_ask_retracted(retract, "kid", 5));

    const std::string still_pending = R"({"event":{
        "type":"child_elicitation_update","session":"kid","elicitation":4,
        "pending":{"message":"still asking"}}})";
    CHECK(!el::fold_child_ask_retracted(still_pending, "kid", 4));

    CHECK(el::answered_summary(R"({"q1":"CAT","q2":["A","B"]})") ==
          "q1: CAT  ·  q2: A, B");
    CHECK(el::answered_summary("").empty());
    CHECK(el::answered_summary("{}").empty());
}

static void test_typed_and_cap() {
    std::printf("typed values and the escaped cap\n");
    PendingAsk ask;
    ask.questions = el::parse_schema(
        R"({"properties":{"n":{"type":"number"},"i":{"type":"integer"},
            "b":{"type":"boolean"},"s":{"type":"string"}}})",
        {});
    CHECK(ask.questions.size() == 4);
    CHECK(find_q(ask.questions, "n")->value_type == api::AskValueType::Number);
    CHECK(find_q(ask.questions, "i")->value_type == api::AskValueType::Integer);
    CHECK(find_q(ask.questions, "b")->value_type == api::AskValueType::Boolean);
    CHECK(find_q(ask.questions, "s")->value_type == api::AskValueType::String);

    AskAnswer a;
    a.text["n"] = " 4.5 ";
    a.text["i"] = "42";
    a.text["b"] = "true";
    a.text["s"] = "42";
    const json c = el::content_object(ask, a);
    CHECK(c["n"].is_number_float() && c["n"] == 4.5);
    CHECK(c["i"].is_number_integer() && c["i"] == 42);
    CHECK(c["b"].is_boolean() && c["b"] == true);
    CHECK(c["s"].is_string() && c["s"] == "42");

    AskAnswer junk;
    junk.text["i"] = "not a number";
    CHECK(el::content_object(ask, junk)["i"] == "not a number");

    PendingAsk one;
    one.questions =
        el::parse_schema(R"({"properties":{"q1":{"type":"string"}}})", {});
    AskAnswer fits;
    fits.text["q1"] = std::string(1000, 'x');
    CHECK(el::answer_within_cap(one, fits));
    AskAnswer quoted;
    quoted.text["q1"] = std::string(90000, '"');
    CHECK(el::content_object(one, quoted).dump().size() <
          el::kContentCapBytes);
    CHECK(!el::answer_within_cap(one, quoted));
    CHECK(el::escaped_content_len(el::content_object(one, quoted)) >
          el::kContentCapBytes);
}

static void test_has_content_matches_the_object() {
    std::printf("the submit gate agrees with the payload\n");
    PendingAsk ask;
    ask.questions = el::parse_schema(
        R"({"properties":{"q1":{"oneOf":[{"const":"a"}]},
            "q1_other":{"type":"string"},"q2":{"type":"string"},
            "q3":{"type":"string"}}})",
        {"q3"});
    const std::vector<AskAnswer> cases = [] {
        std::vector<AskAnswer> v(6);
        v[1].text["q2"] = "  ";
        v[2].text["q2"] = "yes";
        v[3].picks["q1"] = {"a"};
        v[4].text["q1_other"] = "mine";
        v[5].text["q3"] = "a file cannot go this way";
        return v;
    }();
    for (const AskAnswer& a : cases)
        CHECK(el::answer_has_content(ask, a) ==
              !el::content_object(ask, a).empty());
}

static void test_child_order() {
    std::printf("child asks sort numerically\n");
    json state = json::parse(R"({"child_pending_elicitations":[
        {"session":"kid","elicitation":{"elicitation":10}},
        {"session":"kid","elicitation":{"elicitation":4}},
        {"session":"aaa","elicitation":{"elicitation":9}}]})");
    const auto asks = el::asks_from_state(state, "own");
    CHECK(asks.size() == 3);
    CHECK(asks[0].child_session == "aaa");
    CHECK(asks[1].child_session == "kid" && asks[1].seq == 4);
    CHECK(asks[2].child_session == "kid" && asks[2].seq == 10);
}

static void test_approval_cap_and_frames() {
    std::printf("approval cap and turn frames\n");
    CHECK(el::capped_input("short") == "short");
    const std::string huge(el::kApprovalInputCap + 500, 'x');
    const std::string capped = el::capped_input(huge);
    CHECK(capped.size() < huge.size());
    CHECK(capped.rfind("… (truncated)") != std::string::npos);
    CHECK(capped.size() == el::kApprovalInputCap + std::string("… (truncated)").size());

    const std::string raised = R"({"type":"frame","seq":71,"event":{
        "type":"elicitation_requested","tool":"AskUserQuestion",
        "message":"Which ledger?","requested_schema":"{}",
        "timeout_ms":600000,"file_keys":["q4"]}})";
    json entry;
    CHECK(el::ask_entry_from_frame(raised, &entry));
    CHECK(entry["elicitation"] == 71);
    CHECK(entry["tool"] == "AskUserQuestion");
    CHECK(entry["message"] == "Which ledger?");
    CHECK(entry["timeout_ms"] == 600000);
    CHECK(entry["file_keys"][0] == "q4");
    CHECK(!entry.contains("kind"));

    const std::string approvalFrame = R"({"type":"frame","seq":9,"event":{
        "type":"elicitation_requested","kind":"approval","input":"{}"}})";
    CHECK(el::ask_entry_from_frame(approvalFrame, &entry));
    CHECK(entry["kind"] == "approval");
    CHECK(entry["elicitation"] == 9);

    CHECK(!el::ask_entry_from_frame(
        R"({"seq":5,"event":{"type":"block"}})", &entry));
    CHECK(!el::ask_entry_from_frame(
        R"({"event":{"type":"elicitation_requested"}})", &entry));
    CHECK(!el::ask_entry_from_frame("not json", &entry));

    CHECK(std::string(el::kAskGoneReason).find("already answered") !=
          std::string::npos);
}

static void test_child_file_questions_are_not_answerable() {
    std::printf("a child's plain-string question is not answerable here\n");
    const std::string schema = R"({"type":"object","properties":{
        "q1":{"type":"string","title":"Say something"},
        "q2":{"type":"string","title":"Pick","oneOf":[{"const":"a"},
                                                      {"const":"b"}]},
        "q2_other":{"type":"string","title":"Other"}}})";
    json state = json::object();
    json kid = json::object();
    kid["session"] = "kid";
    kid["elicitation"] = {{"elicitation", 41},
                          {"message", "the child asks"},
                          {"requested_schema", schema}};
    state["child_pending_elicitations"] = json::array({kid});

    const auto asks = el::asks_from_state(state, "owner");
    CHECK(asks.size() == 1);
    CHECK(asks[0].child_session == "kid");
    CHECK(asks[0].has_file_question());

    bool sawPicker = false;
    for (const auto& q : asks[0].questions) {
        if (q.key == "q1") CHECK(q.control == api::AskControl::File);
        if (q.key == "q2") {
            sawPicker = true;
            CHECK(q.control == api::AskControl::Single);
            CHECK(q.free_text_key == "q2_other");
        }
    }
    CHECK(sawPicker);
    CHECK(asks[0].answerable_questions() == 1);

    json own = json::object();
    own["pending_elicitations"] = json::array({
        {{"elicitation", 41}, {"requested_schema", schema}}});
    const auto mine = el::asks_from_state(own, "owner");
    CHECK(mine.size() == 1);
    CHECK(!mine[0].has_file_question());
    bool hasText = false;
    for (const auto& q : mine[0].questions)
        if (q.control == api::AskControl::Text) hasText = true;
    CHECK(hasText);
}

static void test_the_cause_watermark_orders_frames() {
    std::printf("latest-cause-wins per (session, seq)\n");
    const auto frame = [](std::uint64_t seq, std::uint64_t cause,
                          bool pending) {
        json ev = json::object();
        ev["type"] = "child_elicitation_update";
        ev["session"] = "kid";
        ev["elicitation"] = seq;
        ev["cause"] = cause;
        if (pending)
            ev["pending"] = {{"tool", "AskUserQuestion"},
                             {"message", "hi"},
                             {"requested_schema", ""}};
        return json({{"type", "frame"}, {"seq", cause}, {"event", ev}}).dump();
    };

    std::string session;
    std::uint64_t seq = 0, cause = 0;
    bool known = false, pending = false;
    json entry;
    CHECK(el::fold_child_update(frame(41, 55, false), &session, &seq, &cause,
                                &known, &pending, &entry));
    CHECK(session == "kid");
    CHECK(seq == 41);
    CHECK(cause == 55);
    CHECK(known);
    CHECK(!pending);

    CHECK(el::fold_child_update(frame(41, 41, true), &session, &seq, &cause,
                                &known, &pending, &entry));
    CHECK(cause == 41);
    CHECK(pending);

    json noCause = json::parse(frame(41, 55, false));
    noCause["event"].erase("cause");
    CHECK(el::fold_child_update(noCause.dump(), &session, &seq, &cause, &known,
                                &pending, &entry));
    CHECK(!known);
    CHECK(cause == 0);
}

int main() {
    test_shapes();
    test_ordering_and_options();
    test_companion_and_files();
    test_content();
    test_commands();
    test_state_decode();
    test_settlement();
    test_typed_and_cap();
    test_has_content_matches_the_object();
    test_child_order();
    test_approval_cap_and_frames();
    test_child_file_questions_are_not_answerable();
    test_the_cause_watermark_orders_frames();
    if (g_failures == 0) {
        std::printf("elicitation: all checks passed\n");
        return 0;
    }
    std::printf("elicitation: %d failure(s)\n", g_failures);
    return 1;
}
