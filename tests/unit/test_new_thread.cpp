// The two rules the new-thread surface turns on, tested without a window.
//
//   1. TWO-STEP ESCAPE (src/ecs/composer_escape.h). One press used to empty
//      the composer outright. The replacement is a total function, and every
//      arm of it is asserted here -- including the two that are easy to get
//      wrong: a press that had transient UI to dismiss must NOT arm, and an
//      arm must not survive the reader typing.
//
//   2. WHAT A CREATE DID (src/api/create_outcome.h). Six endings, and the one
//      that used to be missing is the difference between "the server said no"
//      and "nobody answered". The second cannot be offered a Retry: the
//      conversation may already exist.
//
// Two invariants are checked over EVERY disposition rather than arm by arm,
// because they are the promises, and an arm added later without them would
// pass a per-arm test suite unchanged:
//
//   * nothing is ever silently dropped, and
//   * a Retry is offered only where a repeat is provably safe.
//
// Pure logic. No ECS, no graphics, no filesystem, no network.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/create_outcome.h"
#include "../../src/ecs/composer_escape.h"
#include "../../src/ecs/composer_notice.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using ecs::model::armed_hint_for;
using ecs::model::ComposerEscapeInput;
using ecs::model::ComposerEscapeState;
using ecs::model::ComposerEscapeStep;
using ecs::model::kComposerEscapeArmedHint;
using ecs::model::kComposerEscapeArmWindowMs;
using ecs::model::kComposerEscapeStagedHint;
using ecs::model::resolve_composer_escape;
using ecs::model::retire_stale_arm;

static ComposerEscapeInput press(const std::string& text,
                                 std::uint64_t now_ms,
                                 std::vector<std::string> files = {}) {
    ComposerEscapeInput in;
    in.field_focused = true;
    in.slot = "pane0/__kickoff__";
    in.text = text;
    in.attachments = std::move(files);
    in.now_ms = now_ms;
    return in;
}

static void test_escape_two_steps() {
    std::printf("escape: a draft takes two presses\n");
    ComposerEscapeState state;
    CHECK(resolve_composer_escape(state, press("hello", 1000)) ==
          ComposerEscapeStep::Armed);
    CHECK(state.armed);
    CHECK(resolve_composer_escape(state, press("hello", 1200)) ==
          ComposerEscapeStep::Cleared);
    CHECK(!state.armed);
    // And the press after the clear is a first press again, on an empty box,
    // so it does not arm anything.
    CHECK(resolve_composer_escape(state, press("", 1300)) ==
          ComposerEscapeStep::ReleasedFocus);
}

static void test_escape_dismisses_transient_first() {
    std::printf("escape: transient UI is always step one\n");
    ComposerEscapeState state;
    ComposerEscapeInput in = press("hello", 1000);
    in.transient_up = true;
    CHECK(resolve_composer_escape(state, in) ==
          ComposerEscapeStep::DismissedTransient);
    // The draft is untouched AND unarmed: the press was spent on the menu.
    CHECK(!state.armed);
    // So the next press is the FIRST press for the draft.
    CHECK(resolve_composer_escape(state, press("hello", 1100)) ==
          ComposerEscapeStep::Armed);
}

static void test_escape_arm_expires() {
    std::printf("escape: an arm expires\n");
    ComposerEscapeState state;
    CHECK(resolve_composer_escape(state, press("hello", 1000)) ==
          ComposerEscapeStep::Armed);
    const std::uint64_t late = 1000 + kComposerEscapeArmWindowMs + 1;
    CHECK(resolve_composer_escape(state, press("hello", late)) ==
          ComposerEscapeStep::Armed);
    CHECK(resolve_composer_escape(state, press("hello", late + 10)) ==
          ComposerEscapeStep::Cleared);
}

static void test_escape_arm_is_bound_to_its_draft_and_slot() {
    std::printf("escape: typing or switching panes retires the arm\n");
    ComposerEscapeState state;
    CHECK(resolve_composer_escape(state, press("hel", 1000)) ==
          ComposerEscapeStep::Armed);
    // The reader typed. A stale arm here would let the very next Escape
    // destroy text the warning was never shown for.
    CHECK(resolve_composer_escape(state, press("hell", 1100)) ==
          ComposerEscapeStep::Armed);
    CHECK(resolve_composer_escape(state, press("hell", 1150)) ==
          ComposerEscapeStep::Cleared);

    CHECK(resolve_composer_escape(state, press("hello", 2000)) ==
          ComposerEscapeStep::Armed);
    ComposerEscapeInput other = press("hello", 2100);
    other.slot = "pane1/other-thread";
    CHECK(resolve_composer_escape(state, other) == ComposerEscapeStep::Armed);

    // retire_stale_arm runs every frame, not only on a press.
    ComposerEscapeState frames;
    CHECK(resolve_composer_escape(frames, press("draft", 3000)) ==
          ComposerEscapeStep::Armed);
    retire_stale_arm(frames, "pane0/__kickoff__",
                     press("draft", 3100).identity(), 3100);
    CHECK(frames.armed);
    retire_stale_arm(frames, "pane0/__kickoff__",
                     press("draft!", 3200).identity(), 3200);
    CHECK(!frames.armed);
    // Staging a file under a live arm retires it too: the composer is holding
    // something different from what the warning was shown for.
    CHECK(resolve_composer_escape(frames, press("draft", 4000)) ==
          ComposerEscapeStep::Armed);
    retire_stale_arm(frames, "pane0/__kickoff__",
                     press("draft", 4100, {"a.png"}).identity(), 4100);
    CHECK(!frames.armed);
}

static void test_escape_attachment_only_is_not_empty() {
    std::printf("escape: staged files are content, and are never taken\n");
    ComposerEscapeState state;
    const std::vector<std::string> two = {"first.png", "second.pdf"};

    // THE BLOCKER. A composer holding files and no typed words answered
    // "empty", and the empty arm CLOSES the surface -- one unwarned press took
    // the reader off a surface with their files on it.
    ComposerEscapeInput closable = press("", 1000, two);
    closable.surface_can_close = true;
    CHECK(closable.has_content());
    CHECK(resolve_composer_escape(state, closable) ==
          ComposerEscapeStep::KeptStaged);

    // And the SECOND press is protected too: there is no text to clear, so a
    // press that "completed" a two-step would have nothing to do but close.
    ComposerEscapeInput again = press("", 1200, two);
    again.surface_can_close = true;
    CHECK(resolve_composer_escape(state, again) ==
          ComposerEscapeStep::KeptStaged);
    // Ten presses later, still protected.
    for (std::uint64_t t = 1400; t < 2400; t += 200) {
        ComposerEscapeInput more = press("", t, two);
        more.surface_can_close = true;
        CHECK(resolve_composer_escape(state, more) ==
              ComposerEscapeStep::KeptStaged);
    }

    // It says the RIGHT sentence: what it refused, not what it will destroy.
    CHECK(std::string(armed_hint_for(closable)) ==
          kComposerEscapeStagedHint);
    CHECK(std::string(armed_hint_for(press("typed", 1000, two))) ==
          kComposerEscapeArmedHint);

    // Transient UI still wins the press, files or no files.
    ComposerEscapeState fresh;
    ComposerEscapeInput menu = press("", 1000, two);
    menu.transient_up = true;
    menu.surface_can_close = true;
    CHECK(resolve_composer_escape(fresh, menu) ==
          ComposerEscapeStep::DismissedTransient);
    CHECK(!fresh.armed);

    // ORDER is part of what was staged: the same two files the other way round
    // is a different draft, so an arm taken for one does not answer for the
    // other.
    ComposerEscapeState ordered;
    CHECK(resolve_composer_escape(ordered, press("keep", 1000, two)) ==
          ComposerEscapeStep::Armed);
    const std::vector<std::string> swapped = {"second.pdf", "first.png"};
    CHECK(resolve_composer_escape(ordered, press("keep", 1100, swapped)) ==
          ComposerEscapeStep::Armed);
    CHECK(resolve_composer_escape(ordered, press("keep", 1200, swapped)) ==
          ComposerEscapeStep::Cleared);

    // A file whose NAME is the draft's text is not the same draft as that text
    // alone -- the identity separator is what makes that true.
    CHECK(press("a.png", 0).identity() !=
          press("", 0, {"a.png"}).identity());

    // Text AND files: the second press clears the TEXT, and the step never
    // says anything that would license taking the files.
    ComposerEscapeState both;
    CHECK(resolve_composer_escape(both, press("words", 1000, two)) ==
          ComposerEscapeStep::Armed);
    CHECK(resolve_composer_escape(both, press("words", 1100, two)) ==
          ComposerEscapeStep::Cleared);
    // ...and with the text now gone, the files still hold the surface.
    ComposerEscapeInput after = press("", 1200, two);
    after.surface_can_close = true;
    CHECK(resolve_composer_escape(both, after) ==
          ComposerEscapeStep::KeptStaged);
}

static void test_escape_empty_box() {
    std::printf("escape: an empty box closes predictably\n");
    ComposerEscapeState state;
    ComposerEscapeInput closable = press("", 1000);
    closable.surface_can_close = true;
    CHECK(resolve_composer_escape(state, closable) ==
          ComposerEscapeStep::ClosedSurface);
    CHECK(!state.armed);

    CHECK(resolve_composer_escape(state, press("", 1100)) ==
          ComposerEscapeStep::ReleasedFocus);

    ComposerEscapeInput unfocused = press("", 1200);
    unfocused.field_focused = false;
    CHECK(resolve_composer_escape(state, unfocused) ==
          ComposerEscapeStep::Ignored);
}

// --- what a create did -----------------------------------------------------

static api::Result<api::CreateOutcome> created_ok() {
    api::CreateOutcome out;
    out.session_id = "s1";
    return api::Result<api::CreateOutcome>::success(std::move(out));
}

static api::Result<api::CreateOutcome> created_input(api::SendFailureKind kind) {
    api::CreateOutcome out;
    out.session_id = "s1";
    out.input_accepted = false;
    out.input_failure = api::SendFailure{kind, "the server said no"};
    return api::Result<api::CreateOutcome>::success(std::move(out));
}

static api::Result<api::CreateOutcome> not_created(api::SendFailureKind kind) {
    api::CreateOutcome out;
    out.created = false;
    out.create_failure = api::SendFailure{kind, "the server said no"};
    return api::Result<api::CreateOutcome>::success(std::move(out));
}

static void test_create_dispositions() {
    std::printf("create: every ending is named\n");

    const auto ok = api::classify_create(created_ok());
    CHECK(ok.disposition == api::CreateDisposition::Created);
    CHECK(ok.ok());
    CHECK(ok.session_id == "s1");
    CHECK(!ok.restore_draft);
    CHECK(!ok.offer_retry);
    CHECK(!ok.park_in_outbox);

    const auto rejected =
        api::classify_create(created_input(api::SendFailureKind::Rejected));
    CHECK(rejected.disposition == api::CreateDisposition::CreatedInputRejected);
    CHECK(rejected.session_id == "s1");
    CHECK(rejected.restore_draft);
    CHECK(rejected.offer_retry);

    const auto unheard =
        api::classify_create(created_input(api::SendFailureKind::Unknown));
    CHECK(unheard.disposition == api::CreateDisposition::CreatedInputUnknown);
    CHECK(unheard.session_id == "s1");
    CHECK(unheard.park_in_outbox);
    CHECK(!unheard.offer_retry);
    CHECK(!unheard.restore_draft);

    const auto refused =
        api::classify_create(not_created(api::SendFailureKind::Rejected));
    CHECK(refused.disposition == api::CreateDisposition::NotCreatedRejected);
    CHECK(refused.session_id.empty());
    CHECK(refused.restore_draft);
    CHECK(refused.offer_retry);

    const auto maybe =
        api::classify_create(not_created(api::SendFailureKind::Unknown));
    CHECK(maybe.disposition == api::CreateDisposition::NotCreatedUnknown);
    CHECK(maybe.restore_draft);
    CHECK(!maybe.offer_retry);

    // A transport that simply threw says nothing about whether the session
    // exists. Unknown is the only honest reading, and it is the arm the old
    // code got wrong: it restored AND would have let a retry through.
    const auto threw = api::classify_create(
        api::Result<api::CreateOutcome>::failure("connection reset"));
    CHECK(threw.disposition == api::CreateDisposition::NotCreatedUnknown);
    CHECK(threw.restore_draft);
    CHECK(!threw.offer_retry);
    CHECK(threw.notice.find("connection reset") != std::string::npos);

    // A cancelled upload is the reader's own decision, so it is definite.
    const auto cancelled =
        api::classify_create(not_created(api::SendFailureKind::Cancelled));
    CHECK(cancelled.disposition == api::CreateDisposition::NotCreatedRejected);
    CHECK(cancelled.offer_retry);
}

static void test_create_invariants() {
    std::printf("create: nothing is dropped, and a retry is never a guess\n");
    const std::vector<api::Result<api::CreateOutcome>> all = {
        created_ok(),
        created_input(api::SendFailureKind::Rejected),
        created_input(api::SendFailureKind::Retryable),
        created_input(api::SendFailureKind::Unknown),
        created_input(api::SendFailureKind::Cancelled),
        not_created(api::SendFailureKind::Rejected),
        not_created(api::SendFailureKind::Retryable),
        not_created(api::SendFailureKind::Unknown),
        not_created(api::SendFailureKind::Cancelled),
        api::Result<api::CreateOutcome>::failure("connection reset"),
    };
    for (const auto& result : all) {
        const api::CreateVerdict v = api::classify_create(result);
        // Nothing is silently dropped: a failing create either hands the text
        // back or holds it durably.
        if (!v.ok())
            CHECK(v.restore_draft || v.park_in_outbox);
        // A Retry is only ever offered where a repeat cannot duplicate.
        if (v.offer_retry)
            CHECK(v.disposition == api::CreateDisposition::NotCreatedRejected ||
                  v.disposition ==
                      api::CreateDisposition::CreatedInputRejected);
        // An unknown fate never offers one, and never auto-retries.
        if (v.disposition == api::CreateDisposition::NotCreatedUnknown ||
            v.disposition == api::CreateDisposition::CreatedInputUnknown)
            CHECK(!v.offer_retry);
        // A failure that says nothing is a failure nobody can act on.
        if (!v.ok()) CHECK(!v.notice.empty());
        // A session id is claimed only where one provably exists.
        if (v.disposition == api::CreateDisposition::NotCreatedRejected ||
            v.disposition == api::CreateDisposition::NotCreatedUnknown)
            CHECK(v.session_id.empty());
    }
}

// --- the one notice row ----------------------------------------------------

static void test_notice_precedence_and_dismiss() {
    std::printf("notices: fixed order, and a dismiss that clears one slot\n");
    using ecs::model::ComposerNotices;
    using ecs::model::NoticeSlot;

    ComposerNotices n;
    CHECK(!n.any());
    CHECK(n.visible_slot() == NoticeSlot::None);
    CHECK(n.visible().empty());
    CHECK(n.dismiss() == NoticeSlot::None);

    n.attachment = "that file is not a kind we take";
    CHECK(n.visible_slot() == NoticeSlot::Attachment);
    n.command = "no compact call yet";
    CHECK(n.visible_slot() == NoticeSlot::Command);
    n.outbox = "one message has not reached the server";
    CHECK(n.visible_slot() == NoticeSlot::Outbox);
    n.send = "nothing was created";
    CHECK(n.visible_slot() == NoticeSlot::Send);
    CHECK(n.visible() == "nothing was created");

    // Dismissing walks DOWN the ladder one rung at a time. The whole point of
    // the row: the next notice takes it rather than everything vanishing.
    CHECK(n.dismiss() == NoticeSlot::Send);
    CHECK(n.visible_slot() == NoticeSlot::Outbox);
    CHECK(n.dismiss() == NoticeSlot::Outbox);
    CHECK(n.visible_slot() == NoticeSlot::Command);
    CHECK(n.dismiss() == NoticeSlot::Command);
    CHECK(n.visible_slot() == NoticeSlot::Attachment);
    CHECK(n.dismiss() == NoticeSlot::Attachment);
    CHECK(!n.any());
}

static void test_outbox_sentence() {
    std::printf("notices: the outbox says what it is holding\n");
    using ecs::model::outbox_notice;
    // Nothing held says nothing at all.
    CHECK(outbox_notice(0, 3).empty());
    const std::string one = outbox_notice(1, 0);
    CHECK(one.find("1 message has not reached the server") !=
          std::string::npos);
    CHECK(one.find("still retrying") != std::string::npos);
    // No attempts yet is not "0 tries", which reads like a refusal to try.
    CHECK(one.find("0 tries") == std::string::npos);
    const std::string many = outbox_notice(3, 4);
    CHECK(many.find("3 messages have not reached the server") !=
          std::string::npos);
    CHECK(many.find("4 tries so far") != std::string::npos);
    CHECK(outbox_notice(2, 1).find("1 try so far") != std::string::npos);
    // It never claims delivery, in any arm -- the outbox retries, it does not
    // confirm, and a sentence that said otherwise would be the worst thing
    // this row could say.
    for (std::size_t held = 1; held <= 3; ++held)
        for (int tries = 0; tries <= 3; ++tries) {
            const std::string text = outbox_notice(held, tries);
            CHECK(text.find("sent") == std::string::npos);
            CHECK(text.find("delivered") == std::string::npos);
        }
}

int main() {
    std::printf("=== new thread: escape + create outcomes + notices ===\n");
    test_escape_two_steps();
    test_escape_dismisses_transient_first();
    test_escape_arm_expires();
    test_escape_arm_is_bound_to_its_draft_and_slot();
    test_escape_attachment_only_is_not_empty();
    test_escape_empty_box();
    test_create_dispositions();
    test_create_invariants();
    test_notice_precedence_and_dismiss();
    test_outbox_sentence();
    if (g_failures == 0) {
        std::printf("PASS: new thread escape + create outcomes + notices\n");
        return 0;
    }
    std::printf("FAIL: %d check(s)\n", g_failures);
    return 1;
}
