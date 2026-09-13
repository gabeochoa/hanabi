// What the two per-thread caches keep, and what they refuse to keep.
//
// Both are pure headers with no graphics and no app state, so the behaviour
// that used to be a comment ("bounded by clearing when the open thread
// changes") can be an assertion instead. Both assertions here were written
// against the OLD code first and both failed:
//
//   pane_state.h did not exist -- the state was five function-local statics in
//   main_pane_system.h that no test could reach, let alone count.
//
//   transcript_render_cache.h cleared its whole map whenever it was handed a
//   different thread id, so the split-view case below measured everything
//   twice per frame, forever.

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

#include "../../src/ecs/pane_state.h"
#include "../../src/ecs/transcript_render_cache.h"
#include "../../src/ui/md_spans.h"
#include "../../src/util/wrap_count.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// --- the pane-state store is bounded, and never at the cost of typing -------
static void test_pane_state_is_bounded() {
    std::printf("test_pane_state_is_bounded\n");
    ecs::model::PaneStateStore store;

    for (int i = 0; i < 500; ++i) store.touch("thread-" + std::to_string(i));
    CHECK(store.size() == ecs::model::kMaxPaneStates);
    // The recently touched survive; the oldest are gone.
    CHECK(store.peek("thread-499") != nullptr);
    CHECK(store.peek("thread-0") == nullptr);

    // Defaults are the first-sight values the five maps used to be seeded
    // with on a miss -- follow armed, no previous offset, velocity unknown.
    const ecs::model::PaneState* fresh = store.peek("thread-499");
    CHECK(fresh != nullptr && fresh->latch.follow);
    CHECK(fresh != nullptr && fresh->latch.prevOffset == -1.0f);
    CHECK(fresh != nullptr && !fresh->haveLastScrollY);
    CHECK(fresh != nullptr && !fresh->unreadComputed);
}

static void test_a_draft_is_never_evicted() {
    std::printf("test_a_draft_is_never_evicted\n");
    ecs::model::PaneStateStore store;

    // Type something into one thread, then visit hundreds of others -- which
    // is a person who left a half-written reply and went to look at the rest
    // of their board.
    store.touch("has-a-draft").replyDraft = "the half-typed reply";
    for (int i = 0; i < 500; ++i) store.touch("other-" + std::to_string(i));

    CHECK(store.size() == ecs::model::kMaxPaneStates);
    CHECK(store.drafts() == 1);
    const ecs::model::PaneState* kept = store.peek("has-a-draft");
    CHECK(kept != nullptr);
    CHECK(kept != nullptr && kept->replyDraft == "the half-typed reply");

    // Once it is sent (the draft cleared), it is ordinary again and the very
    // next round of visits may drop it.
    store.touch("has-a-draft").replyDraft.clear();
    for (int i = 500; i < 1000; ++i) store.touch("other-" + std::to_string(i));
    CHECK(store.drafts() == 0);
    CHECK(store.size() == ecs::model::kMaxPaneStates);
}

// --- the render cache survives a second pane --------------------------------
//
// Split view renders two transcripts in ONE frame by swapping openSession
// around a second render_transcript call, so the cache is handed A, then B,
// then A again next frame. Against the old single-thread cache the second
// get() below returned nullptr every time.
static void test_render_cache_holds_both_panes() {
    std::printf("test_render_cache_holds_both_panes\n");
    ecs::model::TranscriptRenderCache cache;

    ecs::model::MsgRender a;
    a.body = "left pane message";
    a.line_count = 3;
    a.height = 42.0f;
    a.wrap_w = 500.0f;
    ecs::model::MsgRender b = a;
    b.body = "right pane message";
    b.height = 64.0f;

    // Frame 1: left pane measures, then the right pane measures.
    cache.reset_for_thread("left-thread");
    cache.put("m1", a.body, a);
    cache.reset_for_thread("right-thread");
    cache.put("m1", b.body, b);

    // Frame 2: the left pane comes round again and must NOT have to re-measure.
    cache.reset_for_thread("left-thread");
    const ecs::model::MsgRender* hit = cache.get("m1", 500.0f, 500.0f - 10.0f, "left pane message");
    CHECK(hit != nullptr);
    CHECK(hit != nullptr && hit->body == "left pane message");
    CHECK(hit != nullptr && hit->height == 42.0f);

    // And the right pane's own measurement is still its own.
    cache.reset_for_thread("right-thread");
    const ecs::model::MsgRender* other = cache.get("m1", 500.0f, 500.0f - 10.0f, "right pane message");
    CHECK(other != nullptr && other->body == "right pane message");

    CHECK(cache.threads() == 2);
    CHECK(cache.total_size() == 2);

    // A width change is still a miss -- a resize has to re-measure -- for an
    // entry that WRAPPED at its width (unwrapped == false, the default).
    CHECK(cache.get("m1", 501.0f, 501.0f - 10.0f, "right pane message") == nullptr);
}

// An entry that did not wrap at its width is good at every width whose wrap
// width still holds its widest hard line, and at no narrower one -- so a
// resize drag over a message that fits is a hit per frame, not a re-measure.
static void test_an_unwrapped_entry_serves_every_wider_width() {
    std::printf("test_an_unwrapped_entry_serves_every_wider_width\n");
    ecs::model::TranscriptRenderCache cache;
    cache.reset_for_thread("pane/thread");
    ecs::model::MsgRender r;
    r.body = "short";
    r.wrap_w = 500.0f;
    r.height = 24.0f;
    r.natural_advance = 120.0f;   // the widest hard line, at the body font
    r.unwrapped = true;           // 500 - 10 >= 120
    // What the counter's probes recorded: the widest line fit, nothing
    // overflowed -- good at every wrap width from 120 up.
    r.fit_lo = 120.0f;
    r.fit_hi = std::numeric_limits<float>::infinity();
    cache.put("m1", "short", r);

    // Wider, and narrower-but-still-fits: the same entry.
    CHECK(cache.get("m1", 900.0f, 890.0f, "short") == &cache.get("m1", 500.0f, 490.0f, "short")[0]);
    CHECK(cache.get("m1", 131.0f, 121.0f, "short") != nullptr);
    CHECK(cache.get("m1", 130.0f, 120.0f, "short") != nullptr);  // exactly fits
    CHECK(cache.natural() == 3);
    // One wrap width too narrow for the widest line: a real miss.
    CHECK(cache.get("m1", 129.0f, 119.0f, "short") == nullptr);
    CHECK(cache.stale() == 1);

    // An entry nobody recorded an interval for never travels.
    ecs::model::MsgRender wrapped;
    wrapped.body = "a long paragraph";
    wrapped.wrap_w = 500.0f;
    wrapped.height = 96.0f;
    wrapped.natural_advance = 2000.0f;
    wrapped.unwrapped = false;
    cache.put("m2", "a long paragraph", wrapped);
    CHECK(cache.get("m2", 900.0f, 890.0f, "a long paragraph") == nullptr);
    CHECK(cache.get("m2", 501.0f, 491.0f, "a long paragraph") == nullptr);
    CHECK(cache.get("m2", 500.0f, 490.0f, "a long paragraph") != nullptr);

    // The hug rule: same shape, the answer capped at the new max width.
    cache.put_hug("m1|hug", "short", 600.0f, 132.0f, true, 120.0f, 132.0f);
    const auto atNine = cache.hug("m1|hug", 900.0f, 890.0f, "short");
    CHECK(atNine.has_value() && atNine->text_w == 132.0f);
    const auto atNarrow = cache.hug("m1|hug", 131.0f, 121.0f, "short");
    CHECK(atNarrow.has_value() && atNarrow->text_w == 131.0f);
    CHECK(!cache.hug("m1|hug", 129.0f, 119.0f, "short").has_value());
}

// The natural advance of a RICH body must cover the RAW widest line, not only
// the visible one. line_count is count_lines over the raw body (markers
// kept), so at a wrap width where "bold text" fits but "**bold text**" does
// not, the raw count is one line more than the hard count -- and an entry
// marked unwrapped by the visible measure alone would serve that stale count
// at every wider width, folding a 40-line message the direct count would not.
// Same counter and same span parser measured() is built on, a uniform metric
// so the numbers are exact; the max of the two passes is the width from which
// the raw count is the hard count everywhere.
static float uniform_advance(const std::string& s) {
    return static_cast<float>(s.size()) * 7.0f;
}
static void test_a_rich_natural_advance_covers_the_raw_line() {
    std::printf("test_a_rich_natural_advance_covers_the_raw_line\n");
    std::string body;
    for (int i = 0; i < 39; ++i) body += "line\n";
    body += "**bold text**";
    const int hardLines = 40;

    float visibleWidest = 0.0f, rawWidest = 0.0f;
    std::size_t start = 0;
    while (start <= body.size()) {
        const std::size_t nl = body.find('\n', start);
        const std::size_t end = nl == std::string::npos ? body.size() : nl;
        const std::string raw = body.substr(start, end - start);
        rawWidest = std::max(rawWidest, uniform_advance(raw));
        visibleWidest = std::max(
            visibleWidest,
            uniform_advance(hanabi::md::inline_spans(raw, hanabi::md::Palette{}).visible));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    CHECK(visibleWidest == 63.0f);
    CHECK(rawWidest == 91.0f);

    // Visible alone: a width the visible rule accepts where the raw count is
    // already stale.
    const float between = 70.0f;
    CHECK(between >= visibleWidest && between < rawWidest);
    CHECK(hanabi::text::wrapped_line_count(body, between, uniform_advance) == hardLines + 1);

    // The max of both passes: the hard count there and at every wider width.
    const float natural = std::max(visibleWidest, rawWidest);
    CHECK(hanabi::text::wrapped_line_count(body, natural, uniform_advance) == hardLines);
    CHECK(hanabi::text::wrapped_line_count(body, natural + 1.0f, uniform_advance) == hardLines);
    CHECK(hanabi::text::wrapped_line_count(body, 1000.0f, uniform_advance) == hardLines);
    // And one short of it is not.
    CHECK(hanabi::text::wrapped_line_count(body, natural - 1.0f, uniform_advance) == hardLines + 1);
}

// A message that WRAPS is exact at every wrap width where none of its breaks
// would move -- the half-open interval its counter's probes recorded -- and
// at no other. Both ends bite: the lower bound is a fitting probe (held), the
// upper bound is an overflowing one (not held).
static void test_a_wrapped_entry_serves_its_interval() {
    std::printf("test_a_wrapped_entry_serves_its_interval\n");
    ecs::model::TranscriptRenderCache cache;
    cache.reset_for_thread("pane/thread");
    // textW is the label width; the wrap width the counter compares against
    // is textW less the inset (10 here, as text_wrap_width takes off).
    ecs::model::MsgRender r;
    r.body = "a paragraph that wraps twice at this width";
    r.wrap_w = 310.0f;
    r.line_count = 3;
    r.height = 72.0f;
    r.natural_advance = 700.0f;
    r.unwrapped = false;
    r.fit_lo = 281.5f;   // the widest line that fit
    r.fit_hi = 312.25f;  // the narrowest line-plus-next-word that did not
    const ecs::model::MsgRender* put = &cache.put("m1", r.body, r);

    // Its own width, exactly: the width slot, not the interval.
    CHECK(cache.get("m1", 310.0f, 300.0f, r.body) == put);
    CHECK(cache.natural() == 0);
    // Inside the interval, the fit bound held and the overflow bound not.
    CHECK(cache.get("m1", 291.5f, 281.5f, r.body) == put);
    CHECK(cache.get("m1", 322.0f, 312.0f, r.body) == put);
    CHECK(cache.get("m1", 300.0f, 290.0f, r.body) == put);
    CHECK(cache.natural() == 3);
    // At the overflow bound and below the fit bound: a break would move.
    CHECK(cache.get("m1", 322.25f, 312.25f, r.body) == nullptr);
    CHECK(cache.get("m1", 291.0f, 281.0f, r.body) == nullptr);
    CHECK(cache.get("m1", 900.0f, 890.0f, r.body) == nullptr);
    CHECK(cache.stale() == 3);
    CHECK(cache.natural() == 3);

    // The next width's entry carries its own interval; the pair serves
    // whichever holds, and a width neither holds is still a miss.
    ecs::model::MsgRender r2 = r;
    r2.wrap_w = 340.0f;
    r2.line_count = 2;
    r2.height = 48.0f;
    r2.fit_lo = 312.25f;
    r2.fit_hi = 340.0f;
    cache.put("m1", r.body, r2);
    const auto* atBound = cache.get("m1", 322.25f, 312.25f, r.body);
    CHECK(atBound != nullptr && atBound->line_count == 2);
    const auto* below = cache.get("m1", 300.0f, 290.0f, r.body);
    CHECK(below != nullptr && below->line_count == 3);
    CHECK(cache.get("m1", 350.0f, 340.0f, r.body) == nullptr);
    CHECK(cache.stale() == 4);

    // A changed source drops the interval with the rest of the entry.
    CHECK(cache.get("m1", 300.0f, 290.0f, "edited") == nullptr);
    CHECK(cache.changed() == 1);
}

static void test_render_cache_is_still_bounded() {
    std::printf("test_render_cache_is_still_bounded\n");
    ecs::model::TranscriptRenderCache cache;
    ecs::model::MsgRender r;
    r.wrap_w = 500.0f;

    // Cycling through many threads must not accumulate a map per thread: that
    // is what the old clear-on-change was protecting against and the bound has
    // to keep protecting against it.
    for (int i = 0; i < 200; ++i) {
        cache.reset_for_thread("t" + std::to_string(i));
        cache.put("m1", "source-1", r);
        cache.put("m2", "source-2", r);
    }
    CHECK(cache.threads() == ecs::model::TranscriptRenderCache::kMaxThreads);
    CHECK(cache.total_size() ==
          2 * ecs::model::TranscriptRenderCache::kMaxThreads);
}

// --- two panes on ONE thread do not share what is typed into them ----------
//
// This is the case splitting produces by default: the same thread in both
// panes. Keyed by session id alone -- which is how the store was keyed until
// the pane became a value -- both panes touch() the SAME entry, so the draft
// typed on the left appears on the right, is sent once, and is lost from both;
// the follow-the-bottom latch is shared, so scrolling one pane up makes the
// other jump; and one minimap drag scrubs both rails.
//
// Written against the old key first: with `touch(id)` in place of
// `touch(pane_key(i, id))` the first CHECK below fails on the line it is on,
//
//   FAIL: left->replyDraft == "typed on the left" (line 152)
//   FAIL: right->replyDraft.empty() (line 153)
//
// because there is one entry and both names point at it.
static void test_two_panes_on_one_thread_keep_their_own() {
    std::printf("test_two_panes_on_one_thread_keep_their_own\n");
    ecs::model::PaneStateStore store;
    const std::string id = "the-same-thread";

    ecs::model::PaneState& left = store.touch(ecs::model::pane_key(0, id));
    ecs::model::PaneState& right = store.touch(ecs::model::pane_key(1, id));

    left.replyDraft = "typed on the left";
    left.attachments.push_back(
        api::Attachment{"/tmp/left.pdf", "left.pdf", "application/pdf", "", 10});
    left.latch.follow = false;          // the reader scrolled the left pane up
    left.lastScrollY = 900.0f;
    left.haveLastScrollY = true;

    left.copiedMessageKey = "same-message";
    left.copiedMessageAt = std::chrono::steady_clock::now();
    left.retriedMessageKey = "same-message";
    left.retriedMessageAt = std::chrono::steady_clock::now();
    left.focusedMessageActionKey = "same-message";

    CHECK(left.replyDraft == "typed on the left");
    CHECK(right.replyDraft.empty());
    CHECK(left.attachments.size() == 1);
    CHECK(left.attachments[0].name == "left.pdf");
    CHECK(right.attachments.empty());
    CHECK(left.copiedMessageKey == "same-message");
    CHECK(right.copiedMessageKey.empty());
    CHECK(left.retriedMessageKey == "same-message");
    CHECK(right.retriedMessageKey.empty());
    CHECK(left.focusedMessageActionKey == "same-message");
    CHECK(right.focusedMessageActionKey.empty());
    CHECK(right.latch.follow);          // the right pane is still pinned to the end
    CHECK(!right.haveLastScrollY);
    CHECK(store.size() == 2);

    // And the two keys are stable: coming back to either pane finds its own
    // entry rather than making a third.
    CHECK(&store.touch(ecs::model::pane_key(0, id)) == &left);
    CHECK(&store.touch(ecs::model::pane_key(1, id)) == &right);
    CHECK(store.size() == 2);
    CHECK(store.drafts() == 1);
}

// --- two panes, one thread, two WIDTHS ---------------------------------------
//
// The measurements are keyed by width, and a user bubble is measured at two
// widths (see WidthPair: the maximum text width, then the hugged width that
// falls out of it). Two panes on one thread at two different pane widths --
// which is any split whose divider is not exactly centred -- is therefore FOUR
// widths, and the pair holds two.
//
// Keyed by thread alone, the two panes share one pair: each pane's first ask
// evicts what the other just wrote, every frame, for the life of the split.
// Written against that key first, the loop below reported
//
//   FAIL: cache.absent() + cache.stale() == cold (line 205)
//         (40 misses over 10 frames, against the 4 cold ones)
//
// which is the negative hit rate the WidthPair was introduced to kill, one
// level up. Keyed by pane AND thread it is four cold misses and nothing after.
static void test_two_panes_at_two_widths_do_not_thrash() {
    std::printf("test_two_panes_at_two_widths_do_not_thrash\n");
    ecs::model::TranscriptRenderCache cache;
    const std::string id = "one-long-thread";
    // The left pane is wider than the right: the divider is off centre.
    const float leftMax = 630.0f, leftHug = 458.0f;
    const float rightMax = 420.0f, rightHug = 305.0f;

    const auto measure = [&](const std::string& key, float w) {
        if (cache.get(key, w, w - 10.0f, "same source") != nullptr) return;
        ecs::model::MsgRender r;
        r.wrap_w = w;
        r.height = w * 0.1f;
        cache.put(key, "same source", r);
    };

    for (int frame = 0; frame < 10; ++frame) {
        // The left pane's two passes over the one user bubble...
        cache.reset_for_thread(ecs::model::pane_key(0, id));
        measure("m1", leftMax);
        measure("m1", leftHug);
        // ...then the right pane's two, in the same frame.
        cache.reset_for_thread(ecs::model::pane_key(1, id));
        measure("m1", rightMax);
        measure("m1", rightHug);
    }

    // Four cold misses -- one per (pane, width) -- and not one more.
    const std::size_t cold = 4;
    CHECK(cache.absent() + cache.stale() == cold);
    CHECK(cache.threads() == 2);
}

static void test_same_id_content_change_invalidates_geometry() {
    std::printf("test_same_id_content_change_invalidates_geometry\n");
    ecs::model::TranscriptRenderCache cache;
    cache.reset_for_thread("pane/thread");
    ecs::model::MsgRender old;
    old.body = "old";
    old.wrap_w = 500.0f;
    old.height = 20.0f;
    cache.put("same-id|r", "old", old);
    cache.put_hug("same-id|hug", "old", 600.0f, 300.0f, false, -1.0f, 0.0f);

    CHECK(cache.get("same-id|r", 500.0f, 500.0f - 10.0f, "new content") == nullptr);
    CHECK(!cache.hug("same-id|hug", 600.0f, 590.0f, "new content").has_value());

    ecs::model::MsgRender fresh = old;
    fresh.body = "new content";
    fresh.height = 40.0f;
    cache.put("same-id|r", "new content", fresh);
    const auto* hit = cache.get("same-id|r", 500.0f, 500.0f - 10.0f, "new content");
    CHECK(hit != nullptr && hit->height == 40.0f);
    CHECK(cache.changed() == 1);
}

static void test_unread_tracks_append_prepend_and_replace() {
    std::printf("test_unread_tracks_append_prepend_and_replace\n");
    struct M { std::int64_t created_at; };
    std::vector<M> messages{{10}, {20}, {30}};
    ecs::model::PaneState state;
    ecs::model::TranscriptMutation mutation;
    ecs::model::update_unread(state, messages, 15, mutation);
    CHECK(state.unreadFirst == 1);
    CHECK(state.unreadCount == 2);

    messages.push_back({40});
    mutation = {0, 1, ecs::model::TranscriptMutationKind::Append, 3, 1};
    ecs::model::update_unread(state, messages, 15, mutation);
    CHECK(state.unreadFirst == 1);
    CHECK(state.unreadCount == 3);

    messages.insert(messages.begin(), M{5});
    mutation = {1, 2, ecs::model::TranscriptMutationKind::Prepend, 0, 1};
    ecs::model::update_unread(state, messages, 15, mutation);
    CHECK(state.unreadFirst == 2);
    CHECK(state.unreadCount == 3);

    messages = {{1}, {2}, {3}, {4}, {5}};
    mutation = {2, 3, ecs::model::TranscriptMutationKind::Reset, 0, 5};
    ecs::model::update_unread(state, messages, 15, mutation);
    CHECK(state.unreadFirst == -1);
    CHECK(state.unreadCount == 0);

    messages.push_back({30});
    mutation = {3, 4, ecs::model::TranscriptMutationKind::Append, 5, 1};
    ecs::model::update_unread(state, messages, 15, mutation);
    CHECK(state.unreadFirst == -1);
    CHECK(state.unreadCount == 0);

    ecs::model::PaneState liveState;
    std::vector<M> live{{10}, {20}, {30}};
    ecs::model::TranscriptMutation initial;
    ecs::model::update_unread(liveState, live, 30, initial);
    live.push_back({31});
    live.push_back({32});
    ecs::model::TranscriptMutation appendThenUpdate{
        1, 2, ecs::model::TranscriptMutationKind::Update, 4, 1};
    ecs::model::update_unread(liveState, live, 30, appendThenUpdate);
    CHECK(liveState.unreadFirst == -1);
    CHECK(liveState.unreadCount == 0);
}

int main() {
    std::printf("=== test_pane_memory ===\n");
    test_pane_state_is_bounded();
    test_a_draft_is_never_evicted();
    test_two_panes_on_one_thread_keep_their_own();
    test_render_cache_holds_both_panes();
    test_same_id_content_change_invalidates_geometry();
    test_unread_tracks_append_prepend_and_replace();
    test_two_panes_at_two_widths_do_not_thrash();
    test_an_unwrapped_entry_serves_every_wider_width();
    test_a_rich_natural_advance_covers_the_raw_line();
    test_a_wrapped_entry_serves_its_interval();
    test_render_cache_is_still_bounded();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
