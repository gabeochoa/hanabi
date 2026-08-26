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

#include <cstdio>
#include <string>

#include "../../src/ecs/pane_state.h"
#include "../../src/ecs/transcript_render_cache.h"

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
    cache.put("m1", a);
    cache.reset_for_thread("right-thread");
    cache.put("m1", b);

    // Frame 2: the left pane comes round again and must NOT have to re-measure.
    cache.reset_for_thread("left-thread");
    const ecs::model::MsgRender* hit = cache.get("m1", 500.0f);
    CHECK(hit != nullptr);
    CHECK(hit != nullptr && hit->body == "left pane message");
    CHECK(hit != nullptr && hit->height == 42.0f);

    // And the right pane's own measurement is still its own.
    cache.reset_for_thread("right-thread");
    const ecs::model::MsgRender* other = cache.get("m1", 500.0f);
    CHECK(other != nullptr && other->body == "right pane message");

    CHECK(cache.threads() == 2);
    CHECK(cache.total_size() == 2);

    // A width change is still a miss -- a resize has to re-measure.
    CHECK(cache.get("m1", 501.0f) == nullptr);
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
        cache.put("m1", r);
        cache.put("m2", r);
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
    left.latch.follow = false;          // the reader scrolled the left pane up
    left.lastScrollY = 900.0f;
    left.haveLastScrollY = true;

    CHECK(left.replyDraft == "typed on the left");
    CHECK(right.replyDraft.empty());
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
        if (cache.get(key, w) != nullptr) return;
        ecs::model::MsgRender r;
        r.wrap_w = w;
        r.height = w * 0.1f;
        cache.put(key, r);
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

int main() {
    std::printf("=== test_pane_memory ===\n");
    test_pane_state_is_bounded();
    test_a_draft_is_never_evicted();
    test_two_panes_on_one_thread_keep_their_own();
    test_render_cache_holds_both_panes();
    test_two_panes_at_two_widths_do_not_thrash();
    test_render_cache_is_still_bounded();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
