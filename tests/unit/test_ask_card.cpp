#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/elicitation.h"
#include "../../src/ecs/ask_card.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using api::AskAnswer;
using api::AskControl;
using api::AskKind;
using api::PendingAsk;
namespace ask = hanabi::ask;

static PendingAsk demo_form() {
    PendingAsk a;
    a.seq = 41;
    a.message = "Which ledger do we trust?";
    a.questions = api::elicitation::parse_schema(
        R"({"properties":{
            "q1":{"title":"Which?","oneOf":[{"const":"ledger"},{"const":"promo"}]},
            "q1_other":{"type":"string","title":"Other"},
            "q2":{"type":"array","items":{"anyOf":[{"const":"rows"},
                                                   {"const":"credits"}]}},
            "q3":{"type":"string"},
            "q4":{"type":"string"}}})",
        {"q4"});
    return a;
}

static void test_cursor() {
    std::printf("keyboard cursor\n");
    const PendingAsk a = demo_form();
    const auto run = ask::option_run(a);
    CHECK(run.size() == 4);
    CHECK(run[0].first == "q1" && run[0].second == "ledger");
    CHECK(run[3].first == "q2" && run[3].second == "credits");

    ask::Cursor c;
    CHECK(!c.set());
    ask::move_cursor(a, &c, 1);
    CHECK(c.question == "q1" && c.option == "ledger");
    ask::move_cursor(a, &c, 1);
    CHECK(c.option == "promo");
    ask::move_cursor(a, &c, 1);
    CHECK(c.question == "q2" && c.option == "rows");
    ask::move_cursor(a, &c, 1);
    ask::move_cursor(a, &c, 1);
    CHECK(c.question == "q2" && c.option == "credits");
    ask::move_cursor(a, &c, -1);
    CHECK(c.option == "rows");

    ask::Cursor up;
    ask::move_cursor(a, &up, -1);
    CHECK(up.question == "q2" && up.option == "credits");

    ask::Cursor stale;
    stale.question = "gone";
    stale.option = "gone";
    ask::move_cursor(a, &stale, 1);
    CHECK(stale.question == "q1");

    PendingAsk textOnly;
    textOnly.questions = api::elicitation::parse_schema(
        R"({"properties":{"q1":{"type":"string"}}})", {});
    ask::Cursor none;
    ask::move_cursor(textOnly, &none, 1);
    CHECK(!none.set());
}

static void test_toggle() {
    std::printf("toggling options\n");
    const PendingAsk a = demo_form();
    AskAnswer answer;

    ask::toggle(a, "q1", "ledger", &answer);
    CHECK(answer.picked("q1", "ledger"));
    ask::toggle(a, "q1", "promo", &answer);
    CHECK(answer.picked("q1", "promo"));
    CHECK(!answer.picked("q1", "ledger"));
    ask::toggle(a, "q1", "promo", &answer);
    CHECK(!answer.picked("q1", "promo"));

    ask::toggle(a, "q2", "rows", &answer);
    ask::toggle(a, "q2", "credits", &answer);
    CHECK(answer.picked("q2", "rows"));
    CHECK(answer.picked("q2", "credits"));
    ask::toggle(a, "q2", "rows", &answer);
    CHECK(!answer.picked("q2", "rows"));
    CHECK(answer.picked("q2", "credits"));

    ask::toggle(a, "q3", "anything", &answer);
    CHECK(answer.picks.find("q3") == answer.picks.end());
    ask::toggle(a, "q4", "anything", &answer);
    CHECK(answer.picks.find("q4") == answer.picks.end());

    AskAnswer viaCursor;
    ask::Cursor c;
    ask::move_cursor(a, &c, 1);
    CHECK(ask::toggle_at_cursor(a, c, &viaCursor));
    CHECK(viaCursor.picked("q1", "ledger"));
    ask::Cursor empty;
    CHECK(!ask::toggle_at_cursor(a, empty, &viaCursor));
}

static void test_submit_gate() {
    std::printf("submit gating\n");
    const PendingAsk a = demo_form();
    AskAnswer answer;
    CHECK(ask::submit_blocked(a, answer));
    CHECK(ask::blocked_reason(a) == "Answer any one of these to submit");

    answer.text["q3"] = "   ";
    CHECK(ask::submit_blocked(a, answer));
    answer.text["q3"] = "one thing";
    CHECK(!ask::submit_blocked(a, answer));

    AskAnswer fileOnly;
    fileOnly.text["q4"] = "cannot go this way";
    CHECK(ask::submit_blocked(a, fileOnly));

    PendingAsk approval;
    approval.kind = AskKind::Approval;
    CHECK(!ask::submit_blocked(approval, AskAnswer{}));

    PendingAsk single;
    single.questions = api::elicitation::parse_schema(
        R"({"properties":{"q1":{"type":"string"}}})", {});
    CHECK(ask::blocked_reason(single) == "Answer to submit");
}

static void test_return_intent() {
    std::printf("what Return means\n");
    const PendingAsk a = demo_form();
    AskAnswer answer;
    ask::Cursor cursor;

    CHECK(ask::return_intent(a, answer, cursor) == ask::ReturnIntent::Ignore);
    ask::move_cursor(a, &cursor, 1);
    CHECK(ask::return_intent(a, answer, cursor) ==
          ask::ReturnIntent::PickAtCursor);
    ask::toggle_at_cursor(a, cursor, &answer);
    CHECK(ask::return_intent(a, answer, cursor) == ask::ReturnIntent::Submit);

    PendingAsk approval;
    approval.kind = AskKind::Approval;
    CHECK(ask::return_intent(approval, AskAnswer{}, ask::Cursor{}) ==
          ask::ReturnIntent::Submit);
}

static void test_geometry() {
    std::printf("card geometry\n");
    const PendingAsk a = demo_form();
    const float one = ask::card_h(a, 1, false);
    const float two = ask::card_h(a, 2, false);
    CHECK(two - one == ask::kMessageH);
    CHECK(ask::card_h(a, 1, true) - one == ask::kNoteH);
    CHECK(one > ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);

    PendingAsk quiet;
    quiet.kind = AskKind::Approval;
    CHECK(ask::card_h(quiet, 0, false) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);
    quiet.input = "{\"command\":\"rm\"}";
    CHECK(ask::card_h(quiet, 0, false) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH + ask::kNoteH);

    CHECK(ask::clamp_message_lines(0) == 1);
    CHECK(ask::clamp_message_lines(9) == ask::kMaxMessageLines);

    PendingAsk broken;
    broken.schema_unreadable = true;
    CHECK(ask::card_h(broken, 0, false) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH + ask::kNoteH);

    const auto* q1 = &a.questions[0];
    CHECK(ask::question_h(*q1) ==
          ask::kPromptH + ask::kOptionH * 2.0f + ask::kFieldH + ask::kNoteH +
              ask::kQuestionGap);
    for (const auto& q : a.questions)
        if (q.control == AskControl::File)
            CHECK(ask::question_h(q) ==
                  ask::kPromptH + ask::kNoteH + ask::kQuestionGap);
}

static void test_head_and_drafts() {
    std::printf("heads and drafts\n");
    PendingAsk a = demo_form();
    CHECK(ask::head_text(a) == "The agent is asking");
    a.child_session = "kid";
    CHECK(ask::head_text(a) == "A sub-agent is asking");
    a.kind = AskKind::Approval;
    CHECK(ask::head_text(a) == "A sub-agent needs approval");
    a.child_session.clear();
    CHECK(ask::head_text(a) == "The agent needs approval");

    ask::State state;
    state.answer_for("#41").text["q3"] = "half typed";
    state.cursor_for("#41").question = "q1";
    CHECK(state.answer_for("#41").text["q3"] == "half typed");
    state.errorId = "#41";
    state.errorText = "refused";
    state.busyId = "#41";
    state.forget("#41");
    CHECK(state.answers.find("#41") == state.answers.end());
    CHECK(state.cursors.find("#41") == state.cursors.end());
    CHECK(state.errorText.empty());
    CHECK(state.busyId.empty());

    state.answer_for("#41").text["q3"] = "kept";
    state.forget("kid#41");
    CHECK(state.answer_for("#41").text["q3"] == "kept");
}

int main() {
    test_cursor();
    test_toggle();
    test_submit_gate();
    test_return_intent();
    test_geometry();
    test_head_and_drafts();
    if (g_failures == 0) {
        std::printf("ask card: all checks passed\n");
        return 0;
    }
    std::printf("ask card: %d failure(s)\n", g_failures);
    return 1;
}
