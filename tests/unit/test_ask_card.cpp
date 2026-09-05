#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/elicitation.h"
#include "../../src/ecs/ask_card.h"
#include "../../src/util/wrap_count.h"

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

static PendingAsk approval_with(const std::string& input) {
    PendingAsk a;
    a.kind = AskKind::Approval;
    a.input = input;
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
    const float unbounded = ask::card_h(a, 1, false, 3, 0, m, 0.0f);
    const float two = ask::card_h(a, 2, false, 3, 0, m, 0.0f);
    CHECK(two - unbounded == ask::kMessageH);
    CHECK(ask::card_h(a, 1, true, 3, 0, m, 0.0f) - unbounded ==
          ask::kNoteH * 3.0f);
    CHECK(ask::card_h(a, 1, true, 1, 0, m, 0.0f) - unbounded == ask::kNoteH);
    CHECK(unbounded > ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);

    const float chrome = ask::chrome_h(a, 1, false, 1);
    for (const float budget : {200.0f, 300.0f, 420.0f, 4000.0f}) {
        const float h = ask::card_h(a, 1, false, 1, 0, m, budget);
        CHECK(h <= (budget > unbounded ? unbounded : budget) + 0.01f);
        CHECK(h >= chrome);
    }
    const float floorH = ask::chrome_h(a, 0, true, 3);
    for (const float budget : {40.0f, 80.0f, 120.0f, 200.0f, 395.0f, 431.0f}) {
        const float h = ask::card_h(a, 2, true, 3, 0, m, budget);
        CHECK(h <= (budget > floorH ? budget : floorH) + 0.01f);
    }
    CHECK(ask::message_lines_for(a, 4, true, 1, 4000.0f) == 4);
    CHECK(ask::message_lines_for(a, 4, true, 1, 150.0f) <
          ask::message_lines_for(a, 4, true, 1, 4000.0f));
    CHECK(ask::message_lines_for(a, 4, true, 5, 320.0f) <=
          ask::message_lines_for(a, 4, true, 1, 320.0f));

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
    CHECK(ask::card_h(huge, 2, true, 3, 0, hm, 340.0f) <= 340.0f + 0.01f);
    CHECK(ask::card_h(huge, 2, true, ask::kMaxNoteLines, 0, hm, 340.0f) <=
          340.0f + 0.01f);

    PendingAsk quiet;
    quiet.kind = AskKind::Approval;
    const std::vector<ask::QuestionMetrics> none;
    CHECK(ask::card_h(quiet, 0, false, 1, 0, none, 0.0f) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH);
    quiet.input = "{\"command\":\"rm\"}";
    CHECK(ask::card_h(quiet, 0, false, 1, 1, none, 0.0f) ==
          ask::kPad * 2.0f + ask::kHeadH + ask::kButtonsH + ask::kNoteH);
    CHECK(ask::card_h(quiet, 0, false, 1, 4, none, 0.0f) -
              ask::card_h(quiet, 0, false, 1, 1, none, 0.0f) ==
          ask::kNoteH * 3.0f);
    CHECK(ask::card_h(quiet, 0, false, 1, 40, none, 0.0f) -
              ask::card_h(quiet, 0, false, 1, 1, none, 0.0f) ==
          ask::kNoteH * 39.0f);
    CHECK(ask::clamp_note_lines(99) == ask::kMaxNoteLines);
    CHECK(ask::clamp_note_lines(0) == 1);
    CHECK(ask::clamp_note_lines(4) == 4);

    CHECK(ask::clamp_message_lines(0) == 1);
    CHECK(ask::clamp_message_lines(9) == ask::kMaxMessageLines);
    CHECK(ask::clamp_option_lines(9) == ask::kMaxOptionLines);
    CHECK(ask::clamp_prompt_lines(9) == ask::kMaxPromptLines);
    CHECK(ask::option_row_h(1) == ask::kOptionH);
    CHECK(ask::option_row_h(2) == ask::kOptionH + ask::kOptionLineH);

    PendingAsk broken;
    broken.schema_unreadable = true;
    CHECK(ask::card_h(broken, 0, false, 1, 0, none, 0.0f) ==
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
    const float chrome = ask::chrome_h(a, 1, false, 1);
    const float body = ask::body_h(a, 0, m);
    const float whole = chrome + body;

    const float content = 620.0f;
    for (const float composerChrome : {98.0f, 160.0f, 240.0f}) {
        const float budget = content - composerChrome - 120.0f - 8.0f;
        const float h = ask::card_h(a, 1, false, 1, 0, m, budget);
        CHECK(h + composerChrome + 8.0f <= content - 120.0f + 0.01f ||
              h == chrome + ask::kMinBodyH);
        CHECK(h <= whole + 0.01f);
    }
    const float roomy =
        ask::card_h(a, 1, false, 1, 0, m, 620.0f - 98.0f - 128.0f);
    const float tight =
        ask::card_h(a, 1, false, 1, 0, m, 620.0f - 240.0f - 128.0f);
    CHECK(tight <= roomy);
}

static void test_a_stale_load_cannot_resurrect() {
    std::printf("stale loads cannot resurrect a card\n");
    ask::State state;
    const std::string gone = "s1/41";
    const std::string later = "s1/42";

    const std::uint64_t bStamp = state.next_load_stamp();
    const std::uint64_t aStamp = state.next_load_stamp();
    state.note_drop(gone);

    CHECK(state.ask_is_stale(gone, bStamp));
    CHECK(state.ask_is_stale(gone, aStamp));
    const std::uint64_t fresh = state.next_load_stamp();
    CHECK(!state.ask_is_stale(gone, fresh));

    // The authority is this ask, not its thread. A sibling raised in the same
    // turn rides the same loads and must survive all of them.
    CHECK(!state.ask_is_stale(later, bStamp));
    CHECK(!state.ask_is_stale(later, aStamp));
    CHECK(!state.ask_is_stale(later, fresh));
    CHECK(!state.ask_is_stale("s2/41", bStamp));
}

static void test_a_reserved_note_costs_exactly_one_note() {
    std::printf("the note costs a note, at a real budget\n");
    const PendingAsk a = demo_form();
    const auto m = flat_metrics(a);
    for (const int rows : {1, 2, 3, 5, ask::kMaxNoteLines})
        CHECK(ask::chrome_h(a, 1, true, rows) - ask::chrome_h(a, 1, false, rows)
              == ask::kNoteH * static_cast<float>(rows));
    CHECK(ask::chrome_h(a, 1, true, ask::kMaxNoteLines + 7) ==
          ask::chrome_h(a, 1, true, ask::kMaxNoteLines));
    const float withNote = ask::card_h(a, 1, true, 3, 0, m, 400.0f);
    const float without = ask::card_h(a, 1, false, 3, 0, m, 400.0f);
    CHECK(withNote <= 400.0f + 0.01f);
    CHECK(without <= 400.0f + 0.01f);
    CHECK(withNote >= without);
    CHECK(ask::irreducible_h(a, 4) - ask::irreducible_h(a, 1) ==
          ask::kNoteH * 3.0f);
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

static void test_a_draft_survives_a_refresh() {
    std::printf("a typed answer survives a re-adopt\n");
    {
        ask::State st;
        api::PendingAsk a;
        a.owner_session = "s1";
        a.seq = 41;
        api::PendingAsk b;
        b.owner_session = "s1";
        b.seq = 42;

        st.adopt({a, b}, {});
        st.answers[a.id()].text["q1"] = "half a sentence";
        st.answers[b.id()].text["q1"] = "the other one";

        st.adopt({a, b}, {a, b});
        CHECK(st.answers[a.id()].text["q1"] == "half a sentence");
        CHECK(st.answers[b.id()].text["q1"] == "the other one");

        st.adopt({a}, {a, b});
        CHECK(st.answers[a.id()].text["q1"] == "half a sentence");
        CHECK(st.answers.count(b.id()) == 0);

        st.adopt({}, {a});
        CHECK(st.answers.empty());
    }

    ask::State state;
    api::PendingAsk a;
    a.owner_session = "s1";
    a.seq = 41;
    api::PendingAsk b;
    b.owner_session = "s1";
    b.seq = 42;

    state.answers[a.id()].text["q1"] = "half a sentence";
    state.answers[b.id()].picks["q1"].push_back("ledger");
    state.seenAt[a.id()] = 100;

    for (const auto& id : {a.id(), b.id()})
        CHECK(state.answers.count(id) == 1);

    state.forget(b.id());
    CHECK(state.answers.count(a.id()) == 1);
    CHECK(state.answers[a.id()].text["q1"] == "half a sentence");
    CHECK(state.answers.count(b.id()) == 0);
    CHECK(state.seenAt.count(a.id()) == 1);

    state.forget(a.id());
    CHECK(state.answers.empty());
    CHECK(state.seenAt.empty());
}

static void test_escape_never_throws_away_typing() {
    std::printf("Escape is not destructive over a draft\n");
    const PendingAsk a = demo_form();
    api::AskAnswer empty;
    CHECK(!ask::has_draft(a, empty));
    api::AskAnswer typed;
    typed.text["q3"] = "check the promo ledger first";
    CHECK(ask::has_draft(a, typed));
    api::AskAnswer picked;
    picked.picks["q1"].push_back("ledger");
    CHECK(ask::has_draft(a, picked));
    api::AskAnswer blank;
    blank.text["q3"] = "   ";
    CHECK(!ask::has_draft(a, blank));
}

static void test_only_a_remainder_gets_rebased() {
    std::printf("an owner ask's clock is not restarted by a refresh\n");
    ask::State st;
    api::PendingAsk own;
    own.owner_session = "s1";
    own.seq = 41;
    api::PendingAsk kid;
    kid.owner_session = "s1";
    kid.child_session = "kid";
    kid.seq = 42;

    st.adopt({own, kid}, {});
    st.seenAt[own.id()] = 1000;
    st.seenAt[kid.id()] = 1000;

    st.adopt({own, kid}, {own, kid});
    CHECK(st.seenAt[own.id()] == 1000);
    CHECK(st.seenAt[kid.id()] != 1000);
}

static void test_escape_semantics() {
    std::printf("Escape dismisses, and never over a draft\n");
    PendingAsk approval;
    approval.kind = api::AskKind::Approval;
    approval.owner_session = "s1";
    approval.seq = 7;
    api::AskAnswer none;
    CHECK(!ask::has_draft(approval, none));

    const PendingAsk form = demo_form();
    api::AskAnswer typed;
    typed.text["q3"] = "half a sentence";
    CHECK(ask::has_draft(form, typed));
}

static void test_an_ask_expires_on_its_own_deadline() {
    std::printf("timeout_ms is honoured, and only when it is set\n");
    CHECK(!ask::expired_at(0, 1000, 9999));
    CHECK(!ask::expired_at(-1, 1000, 9999));

    CHECK(!ask::expired_at(60000, 1000, 1000));
    CHECK(!ask::expired_at(60000, 1000, 1060));
    CHECK(ask::expired_at(60000, 1000, 1061));

    CHECK(!ask::expired_at(600000, 5000, 5100));
    CHECK(ask::expired_at(1000, 5000, 5002));
}

static void test_a_draft_is_never_displaced() {
    std::printf("the shown ask is chosen, then drafted, then first\n");
    CHECK(ask::shown_index({}) == 0);
    CHECK(ask::shown_index({{false, false}, {false, false}}) == 0);
    CHECK(ask::shown_index({{false, false}, {false, true}}) == 1);
    CHECK(ask::shown_index({{false, true}, {false, false}}) == 0);
    CHECK(ask::shown_index({{false, false}, {true, false}}) == 1);
    CHECK(ask::shown_index({{true, false}, {false, true}}) == 0);
    CHECK(ask::shown_index({{false, true}, {true, false}}) == 1);
}

static void test_a_stale_load_cannot_retire_a_live_ask() {
    std::printf("an adopt retires only what its own load could have seen\n");
    ask::State st;
    api::PendingAsk old_ask;
    old_ask.owner_session = "s1";
    old_ask.seq = 41;

    const std::uint64_t inflight = st.next_load_stamp();
    st.adopt({old_ask}, {});
    CHECK(!st.born_after(old_ask.id(), inflight));

    st.next_load_stamp();
    api::PendingAsk fresh;
    fresh.owner_session = "s1";
    fresh.seq = 42;
    st.adopt({old_ask, fresh}, {old_ask});
    CHECK(st.born_after(fresh.id(), inflight));
    CHECK(!st.born_after(old_ask.id(), inflight));
}

static void test_dropped_text_is_marked() {
    std::printf("a clamped block says it was cut\n");
    const std::string cut = ask::with_ellipsis("the promo ledger row");
    CHECK(cut.find("\u2026") != std::string::npos);
    CHECK(cut.size() < std::string("the promo ledger row").size() + 4);
    CHECK(ask::with_ellipsis("one") == "one\u2026");
    CHECK(ask::with_ellipsis("trailing  ") == "trailing\u2026");
    CHECK(ask::with_ellipsis("").find("\u2026") != std::string::npos);
}

static void test_the_shared_overlay_set_is_declared() {
    std::printf("every modal sheet is in one list\n");
    ask::KeyOwnership own;
    own.cardFocused = true;
    CHECK(ask::input_live(own));
    own.modalSheet = true;
    CHECK(!ask::input_live(own));
    CHECK(!ask::keys_live(own));
}

static void test_metrics_track_the_questions_not_the_id() {
    std::printf("metrics follow the question list, not the ask id\n");
    api::PendingAsk one;
    one.owner_session = "s1";
    one.child_session = "kid";
    one.seq = 41;
    api::AskQuestion q;
    q.key = "q1";
    q.prompt = "Which?";
    one.questions.push_back(q);

    api::PendingAsk two = one;
    api::AskQuestion q2;
    q2.key = "q2";
    q2.prompt = "And?";
    two.questions.push_back(q2);

    CHECK(one.id() == two.id());
    CHECK(one.questions.size() != two.questions.size());
    CHECK(ask::metrics_key(one) != ask::metrics_key(two));
}

static void test_a_dropped_session_cannot_resurrect_an_ask() {
    std::printf("a stale load cannot revive a resolved ask\n");
    ask::State st;
    api::PendingAsk a;
    a.owner_session = "s1";
    a.seq = 41;

    const std::uint64_t inflight = st.next_load_stamp();
    st.adopt({a}, {});
    st.answers[a.id()].text["q1"] = "typed";

    st.note_drop(a.id());
    st.adopt({}, {a});
    CHECK(st.answers.count(a.id()) == 0);
    CHECK(st.bornStamp.count(a.id()) == 0);

    CHECK(st.ask_is_stale(a.id(), inflight));
    CHECK(!st.born_after(a.id(), inflight));
}

// Resolving one ask must not discard another raised later in the same turn.
//
// The load in flight while the user answers ask A is the load that carries
// ask B. Under session-keyed drop authority, answering A stamped the whole
// thread and that load was thrown away whole: B never reached the card, and
// the draft the user had already typed into it never existed.
static void test_answering_one_ask_keeps_the_next() {
    std::printf("answering one ask keeps the one raised after it\n");
    ask::State st;
    api::PendingAsk a;
    a.owner_session = "s1";
    a.seq = 41;
    api::PendingAsk b;
    b.owner_session = "s1";
    b.seq = 42;

    st.adopt({a}, {});
    st.answers[a.id()].text["q1"] = "yes";

    // A load goes out mid-turn; the agent raises B while it is in flight.
    const std::uint64_t carrying_b = st.next_load_stamp();

    // The user answers A before that load lands.
    st.note_drop(a.id());

    // A is retired for this load and every older one...
    CHECK(st.ask_is_stale(a.id(), carrying_b));
    // ...and B, which the same load carries, is not touched by A's stamp.
    CHECK(!st.ask_is_stale(b.id(), carrying_b));

    // B lands, keeps its identity and takes a draft of its own.
    st.adopt({b}, {a});
    st.answers[b.id()].text["q1"] = "second answer";
    CHECK(st.answers.count(a.id()) == 0);
    CHECK(st.answers.count(b.id()) == 1);
    CHECK(st.answers[b.id()].text["q1"] == "second answer");
    CHECK(st.bornStamp.count(b.id()) == 1);

    // Answering B in turn retires B alone; A stays retired.
    st.note_drop(b.id());
    CHECK(st.ask_is_stale(b.id(), carrying_b));
    CHECK(st.ask_is_stale(a.id(), carrying_b));
    // A newer load speaks for both again.
    const std::uint64_t after = st.next_load_stamp();
    CHECK(!st.ask_is_stale(a.id(), after));
    CHECK(!st.ask_is_stale(b.id(), after));
}

static void test_rescues_do_not_overwrite_each_other() {
    std::printf("every rescued answer survives, per thread\n");
    ask::RescuedDrafts kept;
    kept.keep("s1", "first answer");
    kept.keep("s1", "second answer");
    kept.keep("s2", "other thread");

    const std::string* one = kept.find("s1");
    CHECK(one != nullptr);
    CHECK(one->find("first answer") != std::string::npos);
    CHECK(one->find("second answer") != std::string::npos);
    const std::string* two = kept.find("s2");
    CHECK(two != nullptr && *two == "other thread");

    kept.clear("s1");
    CHECK(kept.find("s1") == nullptr);
    CHECK(kept.find("s2") != nullptr);

    for (int i = 0; i < 100; ++i)
        kept.keep("s" + std::to_string(1000 + i), "x");
    CHECK(kept.bySession.size() <= ask::kMaxRescuedSessions);
    CHECK(kept.order.size() == kept.bySession.size());
}

static void test_clicked_answers_are_rescued_too() {
    std::printf("a click-only answer is rescued, not just typing\n");
    api::PendingAsk ask;
    ask.owner_session = "s1";
    ask.seq = 41;
    api::AskQuestion q;
    q.key = "q1";
    q.prompt = "Which mismatch?";
    q.free_text_key = "q1_other";
    ask.questions.push_back(q);

    api::AskAnswer picked;
    picked.picks["q1"] = {"promo", "ledger"};
    const std::string clicked = ask::draft_text_of(ask, picked);
    CHECK(clicked.find("promo") != std::string::npos);
    CHECK(clicked.find("ledger") != std::string::npos);
    CHECK(clicked.find("Which mismatch?") != std::string::npos);

    api::AskAnswer typed;
    typed.text["q1_other"] = "  the bank feed  ";
    const std::string free = ask::draft_text_of(ask, typed);
    CHECK(free == "the bank feed");

    CHECK(ask::draft_text_of(ask, api::AskAnswer{}).empty());
}

// The honest "you cannot read this, so you cannot approve it" condition.
//
// This replaces input_unreadable(), which asked whether the widest WRAPPED
// span was wider than the column it had just been wrapped to. It never was:
// wrapping to a column is what makes a span fit in it, and the ask card breaks
// over-long tokens, so no input this app can build made that predicate true.
// Approve stayed live behind a guard that was structurally always false.
//
// What actually makes an approval unreadable is vertical: the body view is
// smaller than a single row, so the card draws "Too short to show the
// questions" where the command should be. That is body_too_short(), it is
// measured from the same natural height the body reserves, and submit_ask()
// and the Approve button now both read it.
static void test_an_unreadable_approval_is_not_approvable() {
    std::printf("an approval too short to show is not approvable\n");
    PendingAsk approval;
    approval.kind = AskKind::Approval;
    approval.input = R"({"command":"./release.sh --allow-outside-workspace"})";

    const float natural = ask::body_h(approval, 16, {});
    CHECK(natural == ask::kNoteH * 16.0f);

    // Room for less than one option row: the view collapses and the card says
    // so instead of showing a slice of the command.
    CHECK(ask::body_too_short(ask::body_view_h(natural, 12.0f), natural));
    CHECK(ask::body_view_h(natural, 12.0f) == 0.0f);

    // Room for one note line but still under an option row: same verdict, and
    // the view is exactly the one line the message occupies.
    CHECK(ask::body_view_h(natural, 20.0f) == ask::kNoteH);
    CHECK(ask::body_too_short(ask::body_view_h(natural, 20.0f), natural));

    // Room for more than an option row: NOT too short. The body scrolls, and
    // every span is reachable, so the approval is readable and approvable.
    const float roomy = ask::body_view_h(natural, ask::kOptionH + 1.0f);
    CHECK(roomy == ask::kOptionH + 1.0f);
    CHECK(!ask::body_too_short(roomy, natural));

    // A body with room for all of it is never too short.
    CHECK(!ask::body_too_short(ask::body_view_h(natural, natural), natural));
    CHECK(ask::body_view_h(natural, natural + 50.0f) == natural);

    // An approval with no input reserves nothing and cannot be too short.
    PendingAsk empty;
    empty.kind = AskKind::Approval;
    CHECK(ask::body_h(empty, 0, {}) == 0.0f);
    CHECK(!ask::body_too_short(0.0f, ask::body_h(empty, 0, {})));
}

// The reserve must come from the wrapper that DRAWS, not one that answers a
// different question.
//
// A command line is one long unbreakable token after another, and that is the
// case where the two wrappers disagree: wrapped_line_count() puts an over-long
// word on a line of its own, while the ask card's span pass breaks it. Reserve
// from the counter and the card is half the height the glyphs need, so the
// draw ellipsises and the tail of the command -- the part that says
// --allow-outside-workspace -- is never on screen.
//
// Pinned here on the fixture string itself so a return to the counter is a red
// test rather than a screenshot somebody has to notice.
static void test_a_command_reserves_the_lines_it_draws() {
    std::printf("an approval reserves the spans the draw uses\n");
    // 7px a glyph: narrow enough to stand in for the 340px card's column.
    const auto measure = [](const std::string& s) {
        return static_cast<float>(s.size()) * 7.0f;
    };
    const std::string command =
        R"({"command":"./scripts/release_batch.sh --force --cycle=2026-09 )"
        R"(--ledger=/var/finance/payouts/2026-09/ledger.csv )"
        R"(--out=/var/finance/payouts/2026-09/settled.csv )"
        R"(--notify=payout-owner@example.test --skip-reconcile-check )"
        R"(--allow-outside-workspace"})";
    const float column = 190.0f;

    std::vector<std::pair<std::size_t, std::size_t>> drawn;
    hanabi::text::wrapped_line_spans(command, column, measure, drawn,
                                     /*break_long_words=*/true);
    const int counted =
        hanabi::text::wrapped_line_count(command, column, measure);

    CHECK(static_cast<int>(drawn.size()) > counted);
    CHECK(ask::body_h(approval_with(command),
                      static_cast<int>(drawn.size()), {}) >
          ask::body_h(approval_with(command), counted, {}));

    // Every byte of the command is inside some drawn span -- so a body tall
    // enough for all of them hides nothing.
    CHECK(!drawn.empty());
    CHECK(drawn.front().first == 0);
    CHECK(drawn.back().second == command.size());
    CHECK(command.find("--allow-outside-workspace") >=
          drawn[static_cast<std::size_t>(counted) - 1].first);
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
    test_a_draft_survives_a_refresh();
    test_escape_never_throws_away_typing();
    test_only_a_remainder_gets_rebased();
    test_escape_semantics();
    test_an_ask_expires_on_its_own_deadline();
    test_a_draft_is_never_displaced();
    test_a_stale_load_cannot_retire_a_live_ask();
    test_dropped_text_is_marked();
    test_the_shared_overlay_set_is_declared();
    test_metrics_track_the_questions_not_the_id();
    test_a_dropped_session_cannot_resurrect_an_ask();
    test_answering_one_ask_keeps_the_next();
    test_rescues_do_not_overwrite_each_other();
    test_clicked_answers_are_rescued_too();
    test_an_unreadable_approval_is_not_approvable();
    test_a_command_reserves_the_lines_it_draws();
    if (g_failures == 0) {
        std::printf("ask card: all checks passed\n");
        return 0;
    }
    std::printf("ask card: %d failure(s)\n", g_failures);
    return 1;
}
