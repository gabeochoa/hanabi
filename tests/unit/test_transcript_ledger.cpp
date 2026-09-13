// tests/unit/test_transcript_ledger.cpp
//
// The transcript's layout ledger (src/ecs/transcript_ledger.h), driven with
// integers and a fake ruler: no afterhours, no api types, no text.
//
// What is pinned, in order of what would freeze the app if it slipped:
//   1. the prefix index agrees with a brute-force sum at every row, after
//      random point updates, appends and a rebuild;
//   2. the rows VISITED and MEASURED per frame at one viewport do not grow
//      with the thread length -- 300, 3,000 and 30,000 rows read the same
//      counts within a small constant, on a scroll, a width change, a tail
//      append and a font change;
//   3. the anchor holds the reader's row at the same pixel through a prepend,
//      a height correction above it, an edit of a visible row and a tail
//      append while following;
//   4. nothing estimated is ever inside the returned window: every row in
//      [first, last) is exact after materialize.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "../../src/ecs/transcript_ledger.h"

using ecs::model::Anchor;
using ecs::model::HeightIndex;
using ecs::model::LedgerFacts;
using ecs::model::LedgerWindow;
using ecs::model::RowClass;
using ecs::model::RowGeom;
using ecs::model::RowMeasure;
using ecs::model::TranscriptLedger;
using ecs::model::TranscriptMutation;
using ecs::model::TranscriptMutationKind;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// ---- a fake thread ----------------------------------------------------------
// Row i's true height at column width w is a deterministic function so a
// re-measure at the same width is stable and a different width differs.
struct FakeThread {
    struct Msg {
        std::string id;
        int role;        // 0 user, 1 assistant, 2 tool, 3 event
        float baseH;     // height at w = 700
    };
    std::vector<Msg> msgs;
    std::size_t measures = 0;

    static FakeThread heterogeneous(std::size_t n, unsigned seed = 7) {
        FakeThread t;
        std::mt19937 rng(seed);
        for (std::size_t i = 0; i < n; ++i) {
            Msg m;
            m.id = "m" + std::to_string(i) + "_" + std::to_string(seed);
            const int pick = static_cast<int>(rng() % 10);
            if (pick < 3) { m.role = 0; m.baseH = 40.0f + static_cast<float>(rng() % 60); }
            else if (pick < 6) { m.role = 1; m.baseH = 80.0f + static_cast<float>(rng() % 400); }
            else if (pick < 9) { m.role = 2; m.baseH = 36.0f; }
            else { m.role = 3; m.baseH = 22.0f; }
            t.msgs.push_back(m);
        }
        return t;
    }
    // Grouping is local: consecutive tool rows of length >= 2 form a pile
    // whose lead spans them; followers are hidden.
    RowClass classify(std::size_t i) const {
        RowClass c;
        const Msg& m = msgs[i];
        if (m.role == 3) { c.kind = RowGeom::Event; return c; }
        if (m.role == 2) {
            std::size_t start = i;
            while (start > 0 && msgs[start - 1].role == 2) --start;
            std::size_t end = i;
            while (end + 1 < msgs.size() && msgs[end + 1].role == 2) ++end;
            const std::size_t run = end - start + 1;
            if (run >= 2) {
                c.kind = RowGeom::ToolPile;
                c.span = static_cast<std::uint32_t>(run);
                c.hidden = i != start;
            } else {
                c.kind = RowGeom::ToolBlock;
            }
            return c;
        }
        c.kind = RowGeom::Bubble;
        c.showAuthor = i == 0 || msgs[i - 1].role == 0;
        c.after = (i + 1 == msgs.size() && m.role == 1) ? 20.0f : 0.0f;
        return c;
    }
    RowMeasure measure(std::size_t i, float w) {
        ++measures;
        RowMeasure r;
        const Msg& m = msgs[i];
        if (m.role == 2) {
            // pile lead: sum of the run
            std::size_t end = i;
            float h = 0.0f;
            while (end < msgs.size() && msgs[end].role == 2) { h += msgs[end].baseH; ++end; }
            r.h = h;  // width-free
            return r;
        }
        if (m.role == 3) { r.h = m.baseH; return r; }
        // text: taller when narrower, in 40 px steps of width
        const float step = std::floor(w / 40.0f);
        r.h = m.baseH * (700.0f / (40.0f * std::max(1.0f, step)));
        r.validLo = step * 40.0f;
        r.validHi = (step + 1.0f) * 40.0f;
        return r;
    }
    std::string id_of(std::size_t i) const { return msgs[i].id; }
};

static LedgerFacts facts_at(float w) {
    LedgerFacts f;
    f.column_w = w;
    return f;
}

static TranscriptMutation reset_mut(std::uint64_t rev = 1) {
    return TranscriptMutation{rev - 1, rev, TranscriptMutationKind::Reset, 0, 0};
}

// ---- 1. the index --------------------------------------------------------------
static void test_height_index_agrees_with_brute_force() {
    std::printf("test_height_index_agrees_with_brute_force\n");
    std::mt19937 rng(3);
    std::vector<float> h(10000);
    for (float& x : h) x = static_cast<float>(1 + rng() % 300);
    HeightIndex idx;
    idx.assign(h);
    auto brute_prefix = [&](std::size_t i) {
        float s = 0.0f;
        for (std::size_t k = 0; k < i; ++k) s += h[k];
        return s;
    };
    int bad = 0;
    for (std::size_t i = 0; i <= h.size(); i += 97)
        if (std::fabs(idx.prefix(i) - brute_prefix(i)) > 0.5f) ++bad;
    CHECK(bad == 0);
    // Point updates.
    for (int k = 0; k < 2000; ++k) {
        const std::size_t i = rng() % h.size();
        h[i] = static_cast<float>(rng() % 500);
        idx.set(i, h[i]);
    }
    bad = 0;
    for (std::size_t i = 0; i <= h.size(); i += 131)
        if (std::fabs(idx.prefix(i) - brute_prefix(i)) > 1.0f) ++bad;
    CHECK(bad == 0);
    // Appends.
    for (int k = 0; k < 500; ++k) {
        h.push_back(static_cast<float>(1 + rng() % 200));
        idx.push_back(h.back());
    }
    CHECK(std::fabs(idx.total() - brute_prefix(h.size())) < 2.0f);
    // row_at: for a sampled y, prefix(row) <= y < prefix(row+1).
    bad = 0;
    for (int k = 0; k < 500; ++k) {
        const float y = static_cast<float>(rng() % static_cast<unsigned>(idx.total()));
        const std::size_t r = idx.row_at(y);
        if (!(idx.prefix(r) <= y + 0.01f && y < idx.prefix(r + 1) + 0.01f)) ++bad;
    }
    CHECK(bad == 0);
    CHECK(idx.row_at(-5.0f) == 0);
    CHECK(idx.row_at(idx.total() + 1000.0f) == h.size() - 1);
    // Zero-height rows are stepped over by row_at's caller (anchor_at); the
    // index itself returns the first row whose cumulative height exceeds y.
    HeightIndex z;
    z.assign({10.0f, 0.0f, 0.0f, 10.0f});
    CHECK(z.row_at(10.0f) == 3 || z.row_at(10.0f) == 1);
    CHECK(z.prefix(4) == 20.0f);
}

// ---- 2. the bound ---------------------------------------------------------------
struct FrameCost {
    std::size_t visited = 0, measured = 0;
};

// One frame: sync at facts, materialize around the anchor. Returns what it
// cost and leaves the ledger ready for the next.
static FrameCost frame(TranscriptLedger& led, FakeThread& t,
                       const TranscriptMutation& mut, const LedgerFacts& f,
                       Anchor& anchor, float viewH, LedgerWindow* outWin = nullptr) {
    led.counters().reset();
    led.sync(t.msgs.size(), mut, f,
             [&](std::size_t i) { return t.classify(i); },
             [&](std::size_t i) { return t.id_of(i); });
    LedgerWindow w = led.materialize(anchor, viewH, viewH * 0.5f, 64,
                                     [&](std::size_t i) { return t.measure(i, f.column_w); });
    if (outWin) *outWin = w;
    FrameCost c;
    c.visited = led.counters().rows_visited;
    c.measured = led.counters().rows_measured;
    return c;
}

static bool window_is_exact(const TranscriptLedger& led, const LedgerWindow& w) {
    for (std::size_t r = w.first; r < w.last; ++r)
        if (!led.covered(r)) return false;
    return true;
}

static void test_work_does_not_grow_with_history() {
    std::printf("test_work_does_not_grow_with_history\n");
    // The SAME 200-row tail behind 100, 2,800 and 29,800 rows of history, so
    // the rows in view are identical and only the thread length differs.
    const float viewH = 760.0f;
    struct Row { std::size_t n; FrameCost scroll, width, append, font, settled; };
    std::vector<Row> rows;
    for (std::size_t hist : {std::size_t{100}, std::size_t{2800}, std::size_t{29800}}) {
        FakeThread t = FakeThread::heterogeneous(hist, static_cast<unsigned>(hist));
        FakeThread tail = FakeThread::heterogeneous(200, 77);
        t.msgs.insert(t.msgs.end(), tail.msgs.begin(), tail.msgs.end());
        const std::size_t n = t.msgs.size();
        TranscriptLedger led;
        Anchor anchor;
        LedgerFacts f = facts_at(700.0f);
        TranscriptMutation mut = reset_mut(1);
        LedgerWindow w;
        frame(led, t, mut, f, anchor, viewH, &w);  // cold: classifies all (counted as reindex)
        CHECK(led.counters().reindexed == n);
        // Open at the bottom, following.
        anchor.id = t.id_of(n - 1);
        anchor.row = n - 1;
        anchor.bottom = true;
        frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(window_is_exact(led, w));
        Row r;
        r.n = n;
        r.settled = frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(r.settled.measured == 0);
        // The reader scrolls up to the same tail row in every thread.
        anchor = Anchor{};
        anchor.id = t.id_of(n - 120);
        anchor.row = n - 120;
        anchor.offset = 10.0f;
        r.scroll = frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(window_is_exact(led, w));
        CHECK(w.filled);
        // Width change: nothing eager, only the window re-measures.
        f.column_w = 655.0f;
        r.width = frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(window_is_exact(led, w));
        // Tail append while scrolled up: O(1) classify, no measure in window.
        t.msgs.push_back({"tail" + std::to_string(n), 1, 120.0f});
        mut = TranscriptMutation{1, 2, TranscriptMutationKind::Append, n, 1};
        r.append = frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(window_is_exact(led, w));
        // Font change: epoch bump, window re-measures, history untouched.
        f.font_epoch = 2;
        r.font = frame(led, t, mut, f, anchor, viewH, &w);
        CHECK(window_is_exact(led, w));
        rows.push_back(r);
        std::printf("  n=%6zu settled v/m=%zu/%zu scroll v/m=%zu/%zu width v/m=%zu/%zu append v/m=%zu/%zu font v/m=%zu/%zu\n",
                    n, r.settled.visited, r.settled.measured, r.scroll.visited, r.scroll.measured,
                    r.width.visited, r.width.measured, r.append.visited, r.append.measured,
                    r.font.visited, r.font.measured);
    }
    // The bound: identical visible content costs identical work whatever the
    // history. Exactly equal, because the rows in view are the same rows.
    for (std::size_t k = 1; k < rows.size(); ++k) {
        CHECK(rows[k].scroll.measured == rows[0].scroll.measured);
        CHECK(rows[k].scroll.visited == rows[0].scroll.visited);
        CHECK(rows[k].width.measured == rows[0].width.measured);
        CHECK(rows[k].width.visited == rows[0].width.visited);
        CHECK(rows[k].append.measured == rows[0].append.measured);
        CHECK(rows[k].append.visited == rows[0].append.visited);
        CHECK(rows[k].font.measured == rows[0].font.measured);
        CHECK(rows[k].font.visited == rows[0].font.visited);
    }
    CHECK(rows.back().width.visited < 200);
    CHECK(rows.back().width.measured > 0);
    CHECK(rows.back().append.measured == 0);  // the tail is offscreen: not measured
}

// ---- 3. the anchor --------------------------------------------------------------
static void test_anchor_holds_through_prepend_and_corrections() {
    std::printf("test_anchor_holds_through_prepend_and_corrections\n");
    const float viewH = 600.0f;
    FakeThread t = FakeThread::heterogeneous(2000, 11);
    TranscriptLedger led;
    Anchor anchor;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    LedgerWindow w;
    frame(led, t, mut, f, anchor, viewH, &w);
    // Put the reader in the middle.
    anchor.id = t.id_of(1000);
    anchor.row = 1000;
    anchor.offset = 12.0f;
    frame(led, t, mut, f, anchor, viewH, &w);
    const float y0 = led.scroll_for(anchor, viewH);
    CHECK(std::fabs((led.top_of(1000) + 12.0f) - y0) < 0.01f);

    // (a) prepend 300 rows: same id, same pixel offset into it; the scroll
    // grows by exactly what was inserted above (all estimates at this point).
    FakeThread grown = FakeThread::heterogeneous(300, 99);
    grown.msgs.insert(grown.msgs.end(), t.msgs.begin(), t.msgs.end());
    t.msgs = grown.msgs;
    mut = TranscriptMutation{1, 2, TranscriptMutationKind::Prepend, 0, 300};
    FrameCost c = frame(led, t, mut, f, anchor, viewH, &w);
    CHECK(led.resolve(anchor) == 1300);
    CHECK(led.geom(1300).exact);  // survived the shift
    const float y1 = led.scroll_for(anchor, viewH);
    CHECK(std::fabs((led.top_of(1300) + 12.0f) - y1) < 0.01f);
    CHECK(c.measured <= 4);  // nothing in the window changed
    CHECK(led.counters().reindexed == 2300);

    // (b) a height correction ABOVE the anchor: row 100 was estimated; make
    // its truth 10x and mark it visited (jump there and back).
    t.msgs[100].baseH = 3000.0f;
    led.mark_dirty(100);
    const float aY_before = led.top_of(1300);
    Anchor away;
    away.id = t.id_of(100);
    away.row = 100;
    away.offset = 0.0f;
    frame(led, t, mut, f, away, viewH, &w);
    CHECK(led.geom(100).exact);
    const float aY_after = led.top_of(1300);
    CHECK(aY_after > aY_before + 2000.0f);  // history above moved
    // Back to the reader's anchor: same id, same offset, scroll derived.
    frame(led, t, mut, f, anchor, viewH, &w);
    CHECK(std::fabs(led.scroll_for(anchor, viewH) - (led.top_of(1300) + 12.0f)) < 0.01f);

    // (c) edit a VISIBLE row (the anchor's neighbour below): it grows in
    // place; the anchor row's y is unchanged.
    const float anchorY = led.top_of(1300);
    t.msgs[1301].baseH += 200.0f;
    mut = TranscriptMutation{2, 3, TranscriptMutationKind::Update, 1301, 1};
    frame(led, t, mut, f, anchor, viewH, &w);
    CHECK(std::fabs(led.top_of(1300) - anchorY) < 0.01f);
    CHECK(led.geom(1301).exact);

    // (d) following the bottom: anchor = last row, offset = h - viewH; an
    // append keeps the new last row at the bottom.
    const std::size_t n = t.msgs.size();
    Anchor follow;
    follow.id = t.id_of(n - 1);
    follow.row = n - 1;
    follow.bottom = true;
    frame(led, t, mut, f, follow, viewH, &w);
    CHECK(std::fabs(led.scroll_for(follow, viewH) - (led.total() - viewH)) < 0.01f);
    t.msgs.push_back({"newtail", 1, 150.0f});
    mut = TranscriptMutation{3, 4, TranscriptMutationKind::Append, n, 1};
    follow.id = t.id_of(n);
    follow.row = n;
    frame(led, t, mut, f, follow, viewH, &w);
    CHECK(led.geom(n).exact);
    CHECK(std::fabs(led.scroll_for(follow, viewH) - (led.total() - viewH)) < 0.01f);
    // The old last row lost its outcome line (i == n-1 is a local fact).
    CHECK(led.geom(n - 1).after == 0.0f);
    CHECK(led.geom(n).after == 20.0f);

    // (e) the id is gone (a refetch replaced it): fall back to the row.
    Anchor lost;
    lost.id = "no-such-id";
    lost.row = 5;
    lost.offset = 3.0f;
    CHECK(led.resolve(lost) == 5);
    lost.row = 999999;
    CHECK(led.resolve(lost) == t.msgs.size() - 1);
}

// ---- 4. window contents ---------------------------------------------------------
static void test_window_never_contains_an_estimate_and_covers_the_viewport() {
    std::printf("test_window_never_contains_an_estimate_and_covers_the_viewport\n");
    const float viewH = 500.0f;
    FakeThread t = FakeThread::heterogeneous(5000, 5);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    frame(led, t, mut, f, a, viewH, &w);
    std::mt19937 rng(17);
    int notExact = 0, uncovered = 0;
    for (int k = 0; k < 200; ++k) {
        // Arbitrary jump anywhere, offsets included.
        const std::size_t row = rng() % t.msgs.size();
        a.id = t.id_of(row);
        a.row = row;
        a.offset = static_cast<float>(rng() % 50) - 25.0f;
        if ((k % 7) == 0) f.column_w = 500.0f + static_cast<float>(rng() % 300);
        frame(led, t, mut, f, a, viewH, &w);
        if (!window_is_exact(led, w)) ++notExact;
        // Coverage: the window spans the viewport, except where the list
        // itself ends (a jump near the last rows, or an offset that puts
        // the viewport top above row 0).
        const float top = w.viewport_top;
        if (w.first > 0 && w.top > top + 0.01f) {
            ++uncovered;
            std::printf("  uncovered above: first=%zu top=%.1f vp=%.1f\n", w.first, w.top, top);
        }
        if (w.last < t.msgs.size() && led.top_of(w.last) < top + viewH - 0.01f) {
            ++uncovered;
            std::printf("  uncovered below: last=%zu y=%.1f need=%.1f\n", w.last, led.top_of(w.last), top + viewH);
        }
    }
    CHECK(notExact == 0);
    CHECK(uncovered == 0);
    // Hidden followers inside the window are exact by definition (0 px) and
    // the lead's height covers them.
    bool sawPile = false;
    for (std::size_t r = 0; r < t.msgs.size(); ++r)
        if (led.geom(r).kind == RowGeom::ToolPile && !led.geom(r).hidden) {
            sawPile = true;
            for (std::size_t k = 1; k < led.geom(r).span; ++k) CHECK(led.geom(r + k).hidden);
            break;
        }
    CHECK(sawPile);
}

static void test_dirty_and_epoch_are_lazy() {
    std::printf("test_dirty_and_epoch_are_lazy\n");
    FakeThread t = FakeThread::heterogeneous(1000, 23);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    a.id = t.id_of(500);
    a.row = 500;
    LedgerWindow w;
    frame(led, t, mut, f, a, 400.0f, &w);
    frame(led, t, mut, f, a, 400.0f, &w);
    const std::size_t measuresBefore = t.measures;
    // Fold mode flips: epoch bump, no measure until the next frame's window.
    f.tool_fold_mode = 1;
    led.counters().reset();
    led.sync(t.msgs.size(), mut, f,
             [&](std::size_t i) { return t.classify(i); },
             [&](std::size_t i) { return t.id_of(i); });
    CHECK(t.measures == measuresBefore);
    CHECK(led.counters().rows_classified == 0);
    CHECK(!led.is_exact(500));
    // Width inside every window row's interval: still exact, no measure.
    f.tool_fold_mode = 0;
    frame(led, t, mut, f, a, 400.0f, &w);  // re-measure once under the new epoch
    const std::size_t after = t.measures;
    f.column_w = 701.0f;  // same 40 px step as 700
    FrameCost c = frame(led, t, mut, f, a, 400.0f, &w);
    CHECK(c.measured == 0);
    CHECK(t.measures == after);
    // mark_dirty on a row far away costs nothing now.
    led.mark_dirty(10);
    c = frame(led, t, mut, f, a, 400.0f, &w);
    CHECK(c.measured == 0);
}

// ---- 5. giant pile, bottom follow, clamping, drift ---------------------------------
static void test_a_huge_pile_is_crossed_by_its_span() {
    std::printf("test_a_huge_pile_is_crossed_by_its_span\n");
    // 200 mixed rows, then a pile of 5,000 tool rows, then 200 more.
    FakeThread t = FakeThread::heterogeneous(200, 31);
    for (int k = 0; k < 5000; ++k) t.msgs.push_back({"tool" + std::to_string(k), 2, 36.0f});
    FakeThread tail = FakeThread::heterogeneous(200, 32);
    t.msgs.insert(t.msgs.end(), tail.msgs.begin(), tail.msgs.end());
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    frame(led, t, mut, f, a, 600.0f, &w);
    CHECK(led.geom(200).kind == RowGeom::ToolPile && !led.geom(200).hidden);
    CHECK(led.geom(200).span == 5000);
    CHECK(led.geom(5199).hidden && led.geom(5199).leadDistance == 4999);
    // Anchor just above the pile: the window straddles it.
    a.id = t.id_of(199);
    a.row = 199;
    a.offset = 0.0f;
    FrameCost c = frame(led, t, mut, f, a, 600.0f, &w);
    CHECK(c.visited < 80);   // not 5,000
    CHECK(led.is_exact(200));
    // Anchor at the pile's tail end (a y inside its last pixels) resolves to
    // the lead, not a follower, and the rows after it.
    const float yEnd = led.top_of(200) + led.geom(200).total() - 1.0f;
    Anchor mid = led.anchor_at(yEnd, [&](std::size_t i) { return t.id_of(i); });
    CHECK(mid.row == 200);
    c = frame(led, t, mut, f, mid, 600.0f, &w);
    CHECK(c.visited < 80);
    // A status update on a follower: O(1), lead re-asked, no run walk.
    mut = TranscriptMutation{1, 2, TranscriptMutationKind::Update, 2500, 1};
    c = frame(led, t, mut, f, mid, 600.0f, &w);
    CHECK(led.counters().rows_classified <= 6);
    // Appending a tool row to a pile at the tail is O(1).
    FakeThread t2 = FakeThread::heterogeneous(10, 33);
    for (int k = 0; k < 3000; ++k) t2.msgs.push_back({"t" + std::to_string(k), 2, 36.0f});
    TranscriptLedger led2;
    Anchor a2;
    mut = reset_mut(1);
    frame(led2, t2, mut, f, a2, 600.0f, &w);
    t2.msgs.push_back({"t-new", 2, 36.0f});
    mut = TranscriptMutation{1, 2, TranscriptMutationKind::Append, 3010, 1};
    frame(led2, t2, mut, f, a2, 600.0f, &w);
    CHECK(led2.counters().rows_classified <= 3);
    CHECK(led2.geom(10).span == 3001);
    CHECK(led2.geom(3010).hidden && led2.geom(3010).leadDistance == 3000);
}

static void test_bottom_anchor_follows_a_growing_live_row() {
    std::printf("test_bottom_anchor_follows_a_growing_live_row\n");
    FakeThread t = FakeThread::heterogeneous(400, 41);
    t.msgs.push_back({"live", 1, 100.0f});
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    f.streaming = true;
    f.live_index = 400;
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    const float viewH = 500.0f;
    frame(led, t, mut, f, a, viewH, &w);
    a.id = "live";
    a.row = 400;
    a.bottom = true;
    a.offset = 0.0f;
    // The cold fill of the tail is the open's cost, not a token's.
    frame(led, t, mut, f, a, viewH, &w);
    std::uint64_t rev = 1;
    for (int tok = 0; tok < 30; ++tok) {
        t.msgs[400].baseH += 17.0f;  // the live row grows every frame
        mut = TranscriptMutation{rev, rev + 1, TranscriptMutationKind::Update, 400, 1};
        ++rev;
        FrameCost c = frame(led, t, mut, f, a, viewH, &w);
        // Resolved AFTER this frame's measure: the bottom is the bottom.
        CHECK(std::fabs(led.scroll_for(a, viewH) - (led.total() - viewH)) < 0.01f);
        CHECK(w.filled);
        CHECK(std::fabs(w.viewport_top - led.scroll_for(a, viewH)) < 0.01f);
        CHECK(c.measured <= 3);
    }
}

static void test_materialize_and_scroll_for_agree_at_the_edges() {
    std::printf("test_materialize_and_scroll_for_agree_at_the_edges\n");
    FakeThread t = FakeThread::heterogeneous(3000, 51);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    const float viewH = 700.0f;
    frame(led, t, mut, f, a, viewH, &w);
    // Jump to row 1 with a big negative offset: the ask is above 0, the
    // clamp puts the top at 0, and every row at the top must be exact.
    a.id = t.id_of(1);
    a.row = 1;
    a.offset = -5000.0f;
    frame(led, t, mut, f, a, viewH, &w);
    CHECK(w.viewport_top == 0.0f);
    CHECK(std::fabs(led.scroll_for(a, viewH)) < 0.01f);
    CHECK(w.filled);
    CHECK(w.first == 0);
    // Jump to the last row with a short anchor: the clamp pulls the top up
    // into unmeasured history; those rows must be measured too.
    a.id = t.id_of(2999);
    a.row = 2999;
    a.offset = 0.0f;
    frame(led, t, mut, f, a, viewH, &w);
    CHECK(std::fabs(w.viewport_top - led.scroll_for(a, viewH)) < 0.01f);
    CHECK(w.filled);
    CHECK(std::fabs(w.viewport_top - (led.total() - viewH)) < 0.01f);
    CHECK(w.passes >= 1 && w.passes <= 4);
}

static void test_index_does_not_drift() {
    std::printf("test_index_does_not_drift\n");
    HeightIndex idx;
    std::vector<float> h(20000, 37.25f);
    idx.assign(h);
    std::mt19937 rng(9);
    // A streaming row updated a million times by small amounts.
    double brute = 0.0;
    for (float x : h) brute += x;
    for (int k = 0; k < 1000000; ++k) {
        const std::size_t i = (k % 3 == 0) ? 19999 : rng() % h.size();
        const float nh = 20.0f + static_cast<float>(rng() % 3000) * 0.37f;
        brute += static_cast<double>(nh) - static_cast<double>(h[i]);
        h[i] = nh;
        idx.set(i, nh);
    }
    double check = 0.0;
    for (float x : h) check += x;
    CHECK(std::fabs(idx.total_d() - check) < 0.05);
    CHECK(std::fabs(brute - check) < 1.0);
}

static void test_pile_boundary_regressions() {
    std::printf("test_pile_boundary_regressions\n");
    // (1) a lone tool block, then a second tool row appended: the lone row
    // becomes the lead of a pile of two, in <= 3 classifications.
    FakeThread t;
    t.msgs = {{"u0", 0, 40.0f}, {"a0", 1, 100.0f}, {"t0", 2, 36.0f}};
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    frame(led, t, mut, f, a, 400.0f, &w);
    CHECK(led.geom(2).kind == RowGeom::ToolBlock);
    t.msgs.push_back({"t1", 2, 36.0f});
    mut = TranscriptMutation{1, 2, TranscriptMutationKind::Append, 3, 1};
    frame(led, t, mut, f, a, 400.0f, &w);
    CHECK(led.geom(2).kind == RowGeom::ToolPile && !led.geom(2).hidden);
    CHECK(led.geom(2).span == 2);
    CHECK(led.geom(3).hidden && led.geom(3).leadDistance == 1);
    CHECK(led.geom(2).hiddenAfter == 1);
    CHECK(led.counters().rows_classified <= 3);

    // (2) an anchor on a hidden follower: materialize and scroll_for agree.
    Anchor onFollower;
    onFollower.id = "t1";
    onFollower.row = 3;
    onFollower.offset = 5.0f;
    frame(led, t, mut, f, onFollower, 400.0f, &w);
    CHECK(led.anchor_row(onFollower) == 2);
    CHECK(std::fabs(w.viewport_top - led.scroll_for(onFollower, 400.0f)) < 0.01f);
    CHECK(w.filled);

    // (3) a status update on a follower dirties the lead's height.
    const std::size_t measuresBefore = t.measures;
    t.msgs[3].baseH = 90.0f;
    mut = TranscriptMutation{2, 3, TranscriptMutationKind::Update, 3, 1};
    frame(led, t, mut, f, a, 400.0f, &w);
    CHECK(t.measures > measuresBefore);
    CHECK(led.is_exact(2));
    CHECK(std::fabs(led.geom(2).h - (36.0f + 90.0f)) < 0.01f);

    // A run of hidden thinking-like rows is crossed in O(1) both ways.
    FakeThread h;
    h.msgs.push_back({"lead", 1, 100.0f});
    for (int k = 0; k < 4000; ++k) h.msgs.push_back({"th" + std::to_string(k), 3, 22.0f});
    h.msgs.push_back({"after", 1, 100.0f});
    TranscriptLedger led2;
    Anchor a2;
    // Classify events as hidden here (the reasoning-off case).
    led2.counters().reset();
    led2.sync(h.msgs.size(), reset_mut(1), f,
              [&](std::size_t i) {
                  RowClass c = h.classify(i);
                  if (c.kind == RowGeom::Event) { c.kind = RowGeom::Thinking; c.hidden = true; }
                  return c;
              },
              [&](std::size_t i) { return h.id_of(i); });
    CHECK(led2.geom(0).hiddenAfter == 4000);
    CHECK(led2.geom(4000).leadDistance == 4000);
    led2.counters().reset();
    a2.id = "lead";
    a2.row = 0;
    LedgerWindow w2 = led2.materialize(a2, 400.0f, 200.0f, 64,
                                       [&](std::size_t i) { return h.measure(i, 700.0f); });
    CHECK(led2.counters().rows_visited <= 6);
    CHECK(w2.last == h.msgs.size());
    Anchor back = led2.anchor_at(150.0f, [&](std::size_t i) { return h.id_of(i); });
    CHECK(back.row == 4001);

    // Prepend that joins a tool run: the old lead becomes a follower and
    // every follower's distance points at the new lead.
    FakeThread j;
    j.msgs = {{"t0", 2, 36.0f}, {"t1", 2, 36.0f}, {"t2", 2, 36.0f}, {"a", 1, 100.0f}};
    TranscriptLedger led3;
    Anchor a3;
    frame(led3, j, reset_mut(1), f, a3, 400.0f, &w);
    CHECK(led3.geom(0).kind == RowGeom::ToolPile && led3.geom(0).span == 3);
    FakeThread pre;
    pre.msgs = {{"u", 0, 40.0f}, {"p0", 2, 36.0f}, {"p1", 2, 36.0f}};
    pre.msgs.insert(pre.msgs.end(), j.msgs.begin(), j.msgs.end());
    j.msgs = pre.msgs;
    frame(led3, j, TranscriptMutation{1, 2, TranscriptMutationKind::Prepend, 0, 3}, f, a3, 400.0f, &w);
    CHECK(led3.geom(1).kind == RowGeom::ToolPile && !led3.geom(1).hidden && led3.geom(1).span == 5);
    CHECK(led3.geom(3).hidden && led3.geom(3).leadDistance == 2);
    CHECK(led3.geom(5).hidden && led3.geom(5).leadDistance == 4);
    CHECK(led3.geom(1).hiddenAfter == 4);
    CHECK(led3.skip_hidden_forward(2) == 6);
}

// The loader's streaming shape: every token is TWO notes in one frame -- an
// append of the row (the first time) or an update of it, plus an update of
// the row it follows when the run finishes -- and a token must not cost a
// rebuild. Read through the same log the pane keeps.
static void test_two_notes_a_frame_do_not_rebuild() {
    std::printf("test_two_notes_a_frame_do_not_rebuild\n");
    using ecs::model::TranscriptMutationLog;
    FakeThread t = FakeThread::heterogeneous(800, 61);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutationLog log;
    std::uint64_t rev = 0;
    auto note = [&](TranscriptMutationKind k, std::size_t first, std::size_t count) {
        log.push(TranscriptMutation{rev, rev + 1, k, first, count});
        ++rev;
    };
    note(TranscriptMutationKind::Reset, 0, t.msgs.size());
    Anchor a;
    LedgerWindow w;
    auto frame_log = [&]() {
        led.counters().reset();
        led.sync(t.msgs.size(), log, f,
                 [&](std::size_t i) { return t.classify(i); },
                 [&](std::size_t i) { return t.id_of(i); });
        w = led.materialize(a, 600.0f, 300.0f, 64,
                            [&](std::size_t i) { return t.measure(i, f.column_w); });
    };
    frame_log();
    CHECK(led.counters().reindexed == 800);
    // The user sends: append the prompt, then in the same frame the reply row.
    const std::size_t p = t.msgs.size();
    t.msgs.push_back({"prompt", 0, 40.0f});
    note(TranscriptMutationKind::Append, p, 1);
    t.msgs.push_back({"reply", 1, 10.0f});
    note(TranscriptMutationKind::Append, p + 1, 1);
    a.id = "reply"; a.row = p + 1; a.bottom = true;
    frame_log();
    CHECK(led.counters().reindexed == 0);
    CHECK(led.rows() == p + 2);
    // Thirty tokens: update of the live row + update of the row before it
    // (optimistic prompt sync), both noted, one frame each.
    std::size_t reindexTotal = 0;
    for (int tok = 0; tok < 30; ++tok) {
        t.msgs[p + 1].baseH += 12.0f;
        note(TranscriptMutationKind::Update, p + 1, 1);
        note(TranscriptMutationKind::Update, p, 1);
        frame_log();
        reindexTotal += led.counters().reindexed;
        CHECK(led.counters().rows_classified <= 6);
        CHECK(std::fabs(led.scroll_for(a, 600.0f) - (led.total() - 600.0f)) < 0.01f);
    }
    CHECK(reindexTotal == 0);
    // Run finish: a tool row appended and the reply updated in one frame.
    t.msgs.push_back({"tool", 2, 36.0f});
    note(TranscriptMutationKind::Append, p + 2, 1);
    note(TranscriptMutationKind::Update, p + 1, 1);
    frame_log();
    CHECK(led.counters().reindexed == 0);
    // A consumer that missed more than the log keeps rebuilds, once.
    for (int k = 0; k < 40; ++k) note(TranscriptMutationKind::Update, p, 1);
    frame_log();
    CHECK(led.counters().reindexed == t.msgs.size());
    frame_log();
    CHECK(led.counters().reindexed == 0);
}

// Reasoning and date-divider toggles are classification, not height: every
// row is re-classified (scalar, counted) and NOTHING is measured.
static void test_reasoning_toggle_reclassifies_without_measuring() {
    std::printf("test_reasoning_toggle_reclassifies_without_measuring\n");
    FakeThread t = FakeThread::heterogeneous(3000, 71);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    bool reasoning = false;
    auto classify = [&](std::size_t i) {
        RowClass c = t.classify(i);
        if (c.kind == RowGeom::Event) { c.kind = RowGeom::Thinking; c.hidden = !reasoning; }
        return c;
    };
    auto frame2 = [&]() {
        led.counters().reset();
        led.sync(t.msgs.size(), mut, f, classify, [&](std::size_t i) { return t.id_of(i); });
        const std::size_t before = t.measures;
        w = led.materialize(a, 600.0f, 300.0f, 64, [&](std::size_t i) { return t.measure(i, f.column_w); });
        return t.measures - before;
    };
    frame2();
    a.id = t.id_of(1500); a.row = 1500;
    frame2();
    auto hidden_thinking = [&]() {
        std::size_t k = 0;
        for (std::size_t r = 0; r < led.rows(); ++r)
            k += led.geom(r).hidden && led.geom(r).kind == RowGeom::Thinking;
        return k;
    };
    CHECK(hidden_thinking() > 100);
    reasoning = true;
    f.show_reasoning = true;
    led.counters().reset();
    led.sync(t.msgs.size(), mut, f, classify, [&](std::size_t i) { return t.id_of(i); });
    CHECK(led.counters().reindexed == 3000);   // metadata pass, counted
    CHECK(led.counters().rows_measured == 0);  // no text measured
    CHECK(hidden_thinking() == 0);
    // The now-visible thinking rows in the window get measured when visited.
    const std::size_t measured = frame2();
    CHECK(measured > 0 && measured < 60);
    CHECK(window_is_exact(led, w));
}

static void test_update_past_the_end_and_rekey() {
    std::printf("test_update_past_the_end_and_rekey\n");
    FakeThread t = FakeThread::heterogeneous(50, 81);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    ecs::model::TranscriptMutationLog log;
    std::uint64_t rev = 0;
    auto note = [&](TranscriptMutationKind k, std::size_t first, std::size_t count) {
        log.push(TranscriptMutation{rev, rev + 1, k, first, count});
        ++rev;
    };
    auto sync = [&]() {
        led.counters().reset();
        led.sync(t.msgs.size(), log, f,
                 [&](std::size_t i) { return t.classify(i); },
                 [&](std::size_t i) { return t.id_of(i); });
    };
    note(TranscriptMutationKind::Reset, 0, 50);
    sync();
    // An Update note for a row not yet appended, then the append: no OOB,
    // no rebuild, the row lands classified.
    t.msgs.push_back({"late", 1, 100.0f});
    note(TranscriptMutationKind::Update, 50, 1);
    note(TranscriptMutationKind::Append, 50, 1);
    sync();
    CHECK(led.rows() == 51);
    CHECK(led.counters().reindexed == 0);
    CHECK(led.geom(50).kind == RowGeom::Bubble);
    // A local id replaced by the server's: the anchor taken under the old id
    // still resolves (fallback by row), the new id resolves directly, and
    // the map holds one key per row.
    Anchor a;
    a.id = "late";
    a.row = 50;
    t.msgs[50].id = "srv-9001";
    note(TranscriptMutationKind::Update, 50, 1);
    sync();
    Anchor byNew;
    byNew.id = "srv-9001";
    byNew.row = 0;
    CHECK(led.resolve(byNew) == 50);
    CHECK(led.resolve(a) == 50);  // old id gone: the remembered row
    // Rekey again and again: the map does not grow.
    for (int k = 0; k < 100; ++k) {
        t.msgs[50].id = "srv-" + std::to_string(k);
        note(TranscriptMutationKind::Update, 50, 1);
        sync();
    }
    Anchor last;
    last.id = "srv-99";
    CHECK(led.resolve(last) == 50);
    Anchor stale;
    stale.id = "srv-3";
    stale.row = 7;
    CHECK(led.resolve(stale) == 7);  // dropped key: row fallback, not row 50
}

// The scroll view's content is the rows plus what the column lays out above
// and below them: the clamp must agree with the view's own, or every frame
// at an edge "corrects" the reader by the difference.
static void test_slack_above_and_below_keep_the_edges_honest() {
    std::printf("test_slack_above_and_below_keep_the_edges_honest\n");
    FakeThread t = FakeThread::heterogeneous(200, 91);
    TranscriptLedger led;
    LedgerFacts f = facts_at(700.0f);
    TranscriptMutation mut = reset_mut(1);
    Anchor a;
    LedgerWindow w;
    const float viewH = 600.0f;
    frame(led, t, mut, f, a, viewH, &w);
    led.set_slack_above(44.0f);   // a rollup panel above row 0
    led.set_slack_below(28.0f);   // the trailing pad
    // HOME: the viewport top sits ABOVE row 0 by the panel's height, not at
    // row 0 -- the panel is on screen.
    a = led.anchor_at(-44.0f, [&](std::size_t i) { return t.id_of(i); });
    CHECK(a.row == 0 && std::fabs(a.offset + 44.0f) < 0.01f);
    frame(led, t, mut, f, a, viewH, &w);
    CHECK(std::fabs(w.viewport_top + 44.0f) < 0.01f);
    CHECK(std::fabs(led.scroll_for(a, viewH) + 44.0f) < 0.01f);
    CHECK(w.filled);
    // END: the last viewH pixels include the pad, so the bottom anchor's
    // scroll is total + pad - viewH and does not move frame to frame.
    Anchor follow;
    follow.id = t.id_of(199);
    follow.row = 199;
    follow.bottom = true;
    follow.offset = led.slack_below();  // the caller asks for the pad on screen
    frame(led, t, mut, f, follow, viewH, &w);
    const float s1 = led.scroll_for(follow, viewH);
    CHECK(std::fabs(s1 - (led.total() + 28.0f - viewH)) < 0.01f);
    frame(led, t, mut, f, follow, viewH, &w);
    CHECK(std::fabs(led.scroll_for(follow, viewH) - s1) < 0.01f);
    // A row in flight appears under the last row (the compacting divider):
    // the slack grows by it and the follow scroll moves by exactly that.
    led.set_slack_below(28.0f + 36.0f);
    follow.offset = led.slack_below();
    frame(led, t, mut, f, follow, viewH, &w);
    CHECK(std::fabs(led.scroll_for(follow, viewH) - (s1 + 36.0f)) < 0.01f);
    led.set_slack_below(28.0f);
    follow.offset = led.slack_below();
    frame(led, t, mut, f, follow, viewH, &w);
    CHECK(std::fabs(led.scroll_for(follow, viewH) - s1) < 0.01f);
    // A wheel notch up from the bottom moves by exactly the notch.
    Anchor up = led.anchor_at(s1 - 30.0f, [&](std::size_t i) { return t.id_of(i); });
    frame(led, t, mut, f, up, viewH, &w);
    CHECK(std::fabs(led.scroll_for(up, viewH) - (s1 - 30.0f)) < 0.01f);
}

int main() {
    std::printf("=== test_transcript_ledger ===\n");
    test_height_index_agrees_with_brute_force();
    test_work_does_not_grow_with_history();
    test_anchor_holds_through_prepend_and_corrections();
    test_window_never_contains_an_estimate_and_covers_the_viewport();
    test_dirty_and_epoch_are_lazy();
    test_a_huge_pile_is_crossed_by_its_span();
    test_bottom_anchor_follows_a_growing_live_row();
    test_materialize_and_scroll_for_agree_at_the_edges();
    test_index_does_not_drift();
    test_pile_boundary_regressions();
    test_two_notes_a_frame_do_not_rebuild();
    test_reasoning_toggle_reclassifies_without_measuring();
    test_update_past_the_end_and_rekey();
    test_slack_above_and_below_keep_the_edges_honest();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
