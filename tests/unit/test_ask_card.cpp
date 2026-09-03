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

static std::vector<ask::QuestionMetrics> flat_metrics(const PendingAsk& a) {
    std::vector<ask::QuestionMetrics> m;
    for (const auto& q : a.questions) {
        ask::QuestionMetrics qm;
        qm.option_lines.assign(q.options.size(), 1);
        m.push_back(qm);
    }
    return m;
}

static void test_geometry() {
    std::printf("card geometry\n");
    const PendingAsk a = demo_form();
    const auto m = flat_metrics(a);
    const float unbounded = ask::card_h(a, 1, false, 0, m, 0.0f);
    const float two = ask::card_h(a, 2, false, 0, m, 0.0f);
    CHECK(two - unbounded == ask::kMessageH);
    CHECK(ask::card_h(a, 1, true, 0, m, 0.0f) - unbounded == ask::kNoteH);
    CHECK(unbounded > ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);

    const float chrome = ask::chrome_h(a, 1, false);
    for (const float budget : {200.0f, 300.0f, 420.0f, 4000.0f}) {
        const float h = ask::card_h(a, 1, false, 0, m, budget);
        CHECK(h <= (budget > unbounded ? unbounded : budget) + 0.01f);
        CHECK(h >= chrome);
    }
    const float floorH = ask::chrome_h(a, 0, true);
    for (const float budget : {40.0f, 80.0f, 120.0f, 200.0f, 395.0f, 431.0f}) {
        const float h = ask::card_h(a, 2, true, 0, m, budget);
        CHECK(h <= (budget > floorH ? budget : floorH) + 0.01f);
    }
    CHECK(ask::message_lines_for(a, 4, true, 4000.0f) == 4);
    CHECK(ask::message_lines_for(a, 4, true, 150.0f) <
          ask::message_lines_for(a, 4, true, 4000.0f));

    PendingAsk huge;
    huge.message = a.message;
    for (int qi = 0; qi < 4; ++qi) {
        api::AskQuestion q;
        q.key = "q" + std::to_string(qi);
        q.control = AskControl::Single;
        q.free_text_key = q.key + "_other";
        for (int oi = 0; oi < 4; ++oi)
            q.options.push_back({"v" + std::to_string(oi), "label", ""});
        huge.questions.push_back(q);
    }
    const auto hm = flat_metrics(huge);
    CHECK(ask::body_h(huge, 0, hm) > 600.0f);
    CHECK(ask::card_h(huge, 2, true, 0, hm, 340.0f) <= 340.0f + 0.01f);

    PendingAsk quiet;
    quiet.kind = AskKind::Approval;
    const std::vector<ask::QuestionMetrics> none;
    CHECK(ask::card_h(quiet, 0, false, 0, none, 0.0f) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);
    quiet.input = "{\"command\":\"rm\"}";
    CHECK(ask::card_h(quiet, 0, false, 1, none, 0.0f) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH + ask::kNoteH);
    CHECK(ask::card_h(quiet, 0, false, 4, none, 0.0f) -
              ask::card_h(quiet, 0, false, 1, none, 0.0f) ==
          ask::kNoteH * 3.0f);
    CHECK(ask::clamp_input_lines(99) == ask::kMaxInputLines);
    CHECK(ask::clamp_input_lines(0) == 1);

    CHECK(ask::clamp_message_lines(0) == 1);
    CHECK(ask::clamp_message_lines(9) == ask::kMaxMessageLines);
    CHECK(ask::clamp_option_lines(9) == ask::kMaxOptionLines);
    CHECK(ask::clamp_prompt_lines(9) == ask::kMaxPromptLines);
    CHECK(ask::option_row_h(1) == ask::kOptionH);
    CHECK(ask::option_row_h(2) == ask::kOptionH + ask::kOptionLineH);

    PendingAsk broken;
    broken.schema_unreadable = true;
    CHECK(ask::card_h(broken, 0, false, 0, none, 0.0f) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH + ask::kNoteH);

    const auto& q1 = a.questions[0];
    ask::QuestionMetrics q1m;
    q1m.option_lines.assign(q1.options.size(), 1);
    CHECK(ask::question_h(q1, q1m) ==
          ask::kPromptH + ask::kOptionH * 2.0f + ask::kFieldH + ask::kNoteH +
              ask::kQuestionGap);
    for (const auto& q : a.questions)
        if (q.control == AskControl::File) {
            ask::QuestionMetrics fm;
            CHECK(ask::question_h(q, fm) ==
                  ask::kPromptH + ask::kNoteH + ask::kQuestionGap);
        }
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

static void test_budget_pays_the_composer_first() {
    std::printf("budget counts the whole strip\n");
    const PendingAsk a = demo_form();
    const auto m = flat_metrics(a);
    const float chrome = ask::chrome_h(a, 1, false);
    const float body = ask::body_h(a, 0, m);
    const float whole = chrome + body;

    const float content = 620.0f;
    for (const float composerChrome : {98.0f, 160.0f, 240.0f}) {
        const float budget = content - composerChrome - 120.0f - 8.0f;
        const float h = ask::card_h(a, 1, false, 0, m, budget);
        CHECK(h + composerChrome + 8.0f <= content - 120.0f + 0.01f ||
              h == chrome + ask::kMinBodyH);
        CHECK(h <= whole + 0.01f);
    }
    const float roomy = ask::card_h(a, 1, false, 0, m, 620.0f - 98.0f - 128.0f);
    const float tight = ask::card_h(a, 1, false, 0, m, 620.0f - 240.0f - 128.0f);
    CHECK(tight <= roomy);
}

static void test_a_stale_load_cannot_resurrect() {
    std::printf("stale loads cannot resurrect a card\n");
    ask::State state;
    const std::string sid = "s1";

    const std::uint64_t bStamp = state.next_load_stamp();
    const std::uint64_t aStamp = state.next_load_stamp();
    state.note_drop(sid);

    CHECK(state.load_is_stale(sid, bStamp));
    CHECK(state.load_is_stale(sid, aStamp));
    const std::uint64_t fresh = state.next_load_stamp();
    CHECK(!state.load_is_stale(sid, fresh));
    CHECK(!state.load_is_stale("other", bStamp));
}

static void test_a_reserved_note_costs_exactly_one_note() {
    std::printf("the note costs a note, at a real budget\n");
    const PendingAsk a = demo_form();
    const auto m = flat_metrics(a);
    CHECK(ask::chrome_h(a, 1, true) - ask::chrome_h(a, 1, false) ==
          ask::kNoteH);
    const float withNote = ask::card_h(a, 1, true, 0, m, 400.0f);
    const float without = ask::card_h(a, 1, false, 0, m, 400.0f);
    CHECK(withNote <= 400.0f + 0.01f);
    CHECK(without <= 400.0f + 0.01f);
    CHECK(withNote >= without);
}

static void test_who_owns_the_cards_keys() {
    std::printf("one policy owns the card's keys\n");
    ask::KeyOwnership own;
    CHECK(!ask::keys_live(own));
    own.cardFocused = true;
    CHECK(ask::keys_live(own));

    own.modalSheet = true;
    CHECK(!ask::keys_live(own));
    own.modalSheet = false;

    own.transientUi = true;
    CHECK(!ask::keys_live(own));
    own.transientUi = false;
    CHECK(ask::keys_live(own));

    own.recordingShortcut = true;
    CHECK(!ask::keys_live(own));
    own.recordingShortcut = false;

    own.cardFocused = false;
    own.modalSheet = false;
    own.transientUi = false;
    CHECK(!ask::keys_live(own));
}

int main() {
    test_cursor();
    test_toggle();
    test_submit_gate();
    test_return_intent();
    test_geometry();
    test_head_and_drafts();
    test_budget_pays_the_composer_first();
    test_a_stale_load_cannot_resurrect();
    test_a_reserved_note_costs_exactly_one_note();
    test_who_owns_the_cards_keys();
    if (g_failures == 0) {
        std::printf("ask card: all checks passed\n");
        return 0;
    }
    std::printf("ask card: %d failure(s)\n", g_failures);
    return 1;
}
