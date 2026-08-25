// The arithmetic a windowed digest list is made of (src/ecs/digest_layout.h).
//
// WHY THESE ARE THE ASSERTIONS. `render_digest` now builds only the cards on
// screen, and it decides which those are from a height it computes WITHOUT
// building the card. Two things can go wrong with that and neither is visible
// in a screenshot of a scrolled-to-the-top list:
//
//   * the height the window predicts disagrees with the height the card
//     actually takes, so the spacers are the wrong size and every card below
//     the fold lands at the wrong y -- the scrollbar lies, and clicking a card
//     opens its neighbour;
//   * the window drops a card that is on screen, or keeps building all of
//     them, and the frame is either wrong or still O(catalog).
//
// The first is covered by there being one function and this file pinning what
// it returns for every card shape in the app. The second is covered by driving
// card_window over a synthetic list whose heights are known exactly.
//
// Both groups were written against the pre-window code and both failed:
//
//   card_window did not exist -- "no member named 'card_window' in namespace
//   'ecs::digest'" -- and the height was a five-line expression inside
//   digest_card() with no name and no caller but itself.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/digest_layout.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace dg = ecs::digest;

// A session `age` seconds old, so relative_time() is deterministic against
// wall clock rather than against whenever the suite happens to run.
static api::SessionSummary sess(const std::string& id, int64_t ageSeconds) {
    api::SessionSummary s;
    s.id = id;
    s.title = "a thread";
    s.updated_at = static_cast<int64_t>(std::time(nullptr)) - ageSeconds;
    return s;
}

// --- what a card's second line says, on every shape the app produces --------
static void test_sub_line_shapes() {
    std::printf("test_sub_line_shapes\n");
    std::string scratch;

    // A real backend: no preview. A grouped card shows the bare age, which is
    // the sparse shape; a mixed one states its verdict, which is not.
    api::SessionSummary blocked = sess("a", 3 * 3600);
    blocked.tag = api::ThreadTag::Blocked;
    CHECK(dg::sub_line(blocked, true, scratch) == "3h");
    CHECK(dg::sub_line(blocked, false, scratch) == "waiting on you  \xc2\xb7  3h");

    api::SessionSummary calm = sess("b", 30 * 60);
    CHECK(dg::sub_line(calm, true, scratch) == "30m");
    CHECK(dg::sub_line(calm, false, scratch) == "last active 30m");

    api::SessionSummary running = sess("c", 8 * 3600);
    running.state = api::ThreadState::Running;
    CHECK(dg::sub_line(running, false, scratch) == "running  \xc2\xb7  8h");

    // An unset timestamp has no age, so a calm mixed card has nothing to say.
    api::SessionSummary undated = sess("d", 0);
    undated.updated_at = 0;
    CHECK(dg::sub_line(undated, false, scratch).empty());
    CHECK(dg::sub_line(undated, true, scratch).empty());
    api::SessionSummary undatedDone = undated;
    undatedDone.tag = api::ThreadTag::Done;
    CHECK(dg::sub_line(undatedDone, false, scratch) == "done");

    // The mock: a rich preview. Grouped drops the state phrase before the
    // separator and keeps only what discriminates.
    api::SessionSummary mock = sess("e", 22 * 60);
    mock.preview = "waiting on you \xc2\xb7 22m";
    CHECK(dg::sub_line(mock, true, scratch) == "22m");
    CHECK(dg::sub_line(mock, false, scratch) == "waiting on you \xc2\xb7 22m");

    // A preview that is only a state word restates the section header, so
    // grouped mode falls back to the age.
    api::SessionSummary bare = sess("f", 2 * 86400);
    bare.preview = "self-running";
    CHECK(dg::sub_line(bare, true, scratch) == "2d");
    CHECK(dg::sub_line(bare, false, scratch) == "self-running");

    // A preview with real detail and no separator is kept verbatim.
    api::SessionSummary detail = sess("g", 60);
    detail.preview = "rebasing onto main";
    CHECK(dg::sub_line(detail, true, scratch) == "rebasing onto main");
}

// --- and therefore how tall the card is -------------------------------------
static void test_card_height_follows_the_sub_line() {
    std::printf("test_card_height_follows_the_sub_line\n");
    std::string scratch;

    // Sparse: short and separator-free, so the age rides inline on the title
    // row and the card collapses to one tight row.
    CHECK(dg::sub_is_sparse(""));
    CHECK(dg::sub_is_sparse("3h"));
    CHECK(dg::sub_is_sparse("12mo"));
    CHECK(dg::sub_is_sparse("61%"));
    // Rich: too long for the title row, or carrying a separator.
    CHECK(!dg::sub_is_sparse("last active 30m"));
    CHECK(!dg::sub_is_sparse("rebasing onto main"));
    // Six bytes is the boundary, and a separator alone is enough to disqualify
    // a string that is under it -- "\xc2\xb7" is two bytes, so this is four
    // visible characters and still rich.
    CHECK(dg::sub_is_sparse("123456"));
    CHECK(!dg::sub_is_sparse("1234567"));
    CHECK(!dg::sub_is_sparse("a \xc2\xb7 b"));

    CHECK(dg::card_body_height("3h") == dg::kCardSparseH);
    CHECK(dg::card_body_height("last active 30m") == dg::kCardRichH);
    // The pitch a window counts in is the body plus BOTH margins. If this ever
    // stops matching what digest_card adds to list_extent, every spacer in a
    // scrolled digest list is the wrong size.
    CHECK(dg::card_pitch("3h") == 42.0f);
    CHECK(dg::card_pitch("last active 30m") == 60.0f);

    api::SessionSummary blocked = sess("a", 3 * 3600);
    blocked.tag = api::ThreadTag::Blocked;
    CHECK(dg::card_pitch(blocked, true, scratch) == 42.0f);
    CHECK(dg::card_pitch(blocked, false, scratch) == 60.0f);
}

// --- composing a sub-line does not allocate per card ------------------------
//
// The window asks for 569 of these a frame at a 2000-session catalog. If each
// one heap-allocates, the window has replaced 2276 entities with 569 mallocs
// and the fix is half a fix. The scratch is reused, so once it has been sized
// by the longest line in the list there is nothing left to allocate: this
// asserts the string's buffer address does not move across a whole SECOND
// pass over the same rows. (Not the first: the first pass is where a longer
// line than any seen so far grows the buffer, which is the one-off cost the
// reuse exists to amortize.)
static void test_the_pitch_pass_reuses_one_buffer() {
    std::printf("test_the_pitch_pass_reuses_one_buffer\n");
    std::string scratch;
    std::vector<api::SessionSummary> rows;
    for (int i = 0; i < 500; ++i) {
        api::SessionSummary s = sess("s" + std::to_string(i), (i + 1) * 61);
        s.tag = api::ThreadTag::Blocked;  // the composing branch
        rows.push_back(s);
    }
    for (const auto& s : rows) (void)dg::sub_line(s, false, scratch);
    const void* buf = static_cast<const void*>(scratch.data());
    for (const auto& s : rows) (void)dg::sub_line(s, false, scratch);
    CHECK(static_cast<const void*>(scratch.data()) == buf);
}

// --- the window itself ------------------------------------------------------
//
// A fixed-height list first, because it is the case where the arithmetic can
// be checked by division.
static void test_window_over_uniform_cards() {
    std::printf("test_window_over_uniform_cards\n");
    const int n = 1000;
    const float pitch = 42.0f;  // sparse cards
    auto p = [&](int) { return pitch; };

    // At the top of a 600 px viewport with no motion: 600/42 is 15 cards on
    // screen, plus 180 px of slack below (and none above -- the list starts
    // there), so about 19.
    dg::CardWindow w = dg::card_window(n, p, 600.0f, 0.0f, 0.0f);
    CHECK(w.first == 0);
    CHECK(w.built() >= 15);
    CHECK(w.built() <= 24);
    CHECK(w.above == 0.0f);
    CHECK(w.total == static_cast<float>(n) * pitch);
    // The spacers plus the built cards are the whole list, exactly. This is
    // the property that keeps the scrollbar honest.
    CHECK(w.above + static_cast<float>(w.built()) * pitch + w.below == w.total);

    // Scrolled into the middle: the same amount of work, and the window has
    // moved with the view.
    const float mid = 500.0f * pitch;
    dg::CardWindow m = dg::card_window(n, p, 600.0f, mid, mid);
    CHECK(m.first > 490);
    CHECK(m.first <= 500);
    CHECK(m.last >= 515);
    CHECK(m.built() <= 30);
    CHECK(m.above + static_cast<float>(m.built()) * pitch + m.below == m.total);
    // Every card on screen is inside the window -- that is what "nothing is
    // hidden" means when the thing being measured is a slice.
    CHECK(static_cast<float>(m.first) * pitch <= mid);
    CHECK(static_cast<float>(m.last) * pitch >= mid + 600.0f);

    // At the very bottom, the window does not run off the end.
    const float end = w.total;
    dg::CardWindow e = dg::card_window(n, p, 600.0f, end, end);
    CHECK(e.last == n);
    CHECK(e.below == 0.0f);
    CHECK(e.built() <= 30);
}

// A list of MIXED heights is the case the sidebar never had to solve, and the
// reason this is a prefix sum rather than a division.
static void test_window_over_mixed_heights() {
    std::printf("test_window_over_mixed_heights\n");
    // Alternating 42 and 60: 102 px per pair, 100 pairs.
    const int n = 200;
    auto p = [](int i) { return (i % 2 == 0) ? 42.0f : 60.0f; };
    const float total = 100.0f * 102.0f;

    dg::CardWindow w = dg::card_window(n, p, 600.0f, 0.0f, 0.0f);
    CHECK(w.total == total);
    float built = 0.0f;
    for (int i = w.first; i < w.last; ++i) built += p(i);
    CHECK(w.above + built + w.below == w.total);

    // Scrolled to card 100, whose top is exactly 50 pairs in.
    const float y100 = 50.0f * 102.0f;
    dg::CardWindow m = dg::card_window(n, p, 600.0f, y100, y100);
    CHECK(m.above <= y100);
    CHECK(m.first <= 100);
    // A uniform-pitch approximation would have put `first` at
    // y100/42 == 121 -- past the card that is actually under the top edge, so
    // the top of the viewport would have been empty. This is why the estimate
    // is not an estimate.
    CHECK(m.first < 121);
    built = 0.0f;
    for (int i = m.first; i < m.last; ++i) built += p(i);
    CHECK(m.above + built + m.below == m.total);
}

// The window covers where the view is ABOUT to be, because the build runs
// before the scroll eases. A target far below the offset widens the window
// downward rather than leaving a frame of empty list.
static void test_window_covers_a_pending_scroll() {
    std::printf("test_window_covers_a_pending_scroll\n");
    const int n = 1000;
    auto p = [](int) { return 42.0f; };
    dg::CardWindow still = dg::card_window(n, p, 600.0f, 0.0f, 0.0f);
    dg::CardWindow flung = dg::card_window(n, p, 600.0f, 0.0f, 900.0f);
    CHECK(flung.last > still.last);
    CHECK(static_cast<float>(flung.last) * 42.0f >= 900.0f + 600.0f);
    // And it is BOUNDED: an arbitrarily large fling does not rebuild the list.
    dg::CardWindow huge = dg::card_window(n, p, 600.0f, 0.0f, 40000.0f);
    CHECK(huge.built() < n);
    CHECK(huge.built() <= dg::kMaxWindowCards);
}

// Frame one has measured nothing, so there is no viewport to window against.
// Building the lot is right: the alternative is a blank pane on the first
// frame of every digest screen, which is what a person sees when they click.
static void test_unmeasured_viewport_builds_everything() {
    std::printf("test_unmeasured_viewport_builds_everything\n");
    auto p = [](int) { return 42.0f; };
    dg::CardWindow w = dg::card_window(50, p, 0.0f, 0.0f, 0.0f);
    CHECK(w.whole(50));
    CHECK(w.above == 0.0f);
    CHECK(w.below == 0.0f);
    CHECK(w.total == 50.0f * 42.0f);

    // And an empty list is a window over nothing, not a crash.
    dg::CardWindow z = dg::card_window(0, p, 600.0f, 0.0f, 0.0f);
    CHECK(z.built() == 0);
    CHECK(z.total == 0.0f);
}

int main() {
    std::printf("=== digest layout tests ===\n");
    test_sub_line_shapes();
    test_card_height_follows_the_sub_line();
    test_the_pitch_pass_reuses_one_buffer();
    test_window_over_uniform_cards();
    test_window_over_mixed_heights();
    test_window_covers_a_pending_scroll();
    test_unmeasured_viewport_builds_everything();
    if (g_failures == 0) {
        std::printf("ALL DIGEST LAYOUT TESTS PASSED\n");
        return 0;
    }
    std::printf("%d DIGEST LAYOUT TEST(S) FAILED\n", g_failures);
    return 1;
}
