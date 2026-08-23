// End-to-end / integration tests for hanabi's real app logic, exercised
// HEADLESSLY (no window, no UIContext, no graphics backend) against the
// deterministic MockClient and the real afterhours entity system.
//
// These assert the SHIPPED logic: the sidebar/main-pane/tab systems delegate
// their pure decisions to ecs::model (thread_model.h) and ecs::model
// (tab_model.h), and those exact functions are what we call here — no copies.
//
// Style mirrors tests/unit/test_api.cpp: a tiny assert-based runner, hermetic
// (no network, no env deps).

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

// afterhours ECS core only (headless-safe: no graphics backend linked).
// FMT_HEADER_ONLY so the autolayout include below (which uses fmt::format for
// its debug/overflow messages) links without the fmt library — the test build
// doesn't link libfmt (only the main app does). Must be defined before any
// afterhours header pulls in fmt.
#define FMT_HEADER_ONLY
#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/mock_client.h"
#include "../../src/ecs/components.h"
#include "../../src/ecs/tab_model.h"
#include "../../src/ecs/thread_model.h"
#include "../../src/ecs/transcript_cache.h"
#include "../../src/ui/find_operators.h"

// afterhours UI layout engine (headless: no graphics backend linked — the
// `none` backend's draw_* are no-ops, and autolayout is pure geometry). Used
// by the sidebar-scroll regression test below to assert the folder/thread list
// stacks in ONE column at any scroll position. Included via <angle-brackets> so
// it resolves through the build's `-isystem vendor/` search and its (vendored)
// warnings are treated as system-header warnings — keeping the test build's
// warning output clean (the app never edits vendor/).
#include <afterhours/src/plugins/autolayout.h>

static int g_failures = 0;
static int g_skipped = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// Fresh entity world per test so tab/entity state does not leak between tests.
static void reset_world() {
    afterhours::EntityHelper::delete_all_entities(/*include_permanent=*/true);
}

// ---------------------------------------------------------------------------
// 1) Session list loads from the mock and is sorted newest-first; the expected
//    sample threads are present.
// ---------------------------------------------------------------------------
static void test_list_loads_sorted_and_has_samples() {
    std::printf("test_list_loads_sorted_and_has_samples\n");
    api::MockClient m;
    auto r = m.list_sessions();
    CHECK(r.ok);
    CHECK(r.value.size() >= 10);

    // Sorted strictly newest-first by updated_at.
    for (size_t i = 1; i < r.value.size(); ++i)
        CHECK(r.value[i - 1].updated_at >= r.value[i].updated_at);

    // Expected sample threads present (from the mock seed).
    bool has_t1 = false, has_t4 = false, has_t10 = false;
    for (const auto& s : r.value) {
        if (s.id == "t1") has_t1 = true;   // stickers broke (attention)
        if (s.id == "t4") has_t4 = true;   // finished, wants a read (review)
        if (s.id == "t10") has_t10 = true;  // parent, nothing to report (calm)
    }
    CHECK(has_t1);
    CHECK(has_t4);
    CHECK(has_t10);
}

// ---------------------------------------------------------------------------
// 2) Thread state model: attention / review / running / parked / archived /
//    done classify correctly, and glyph selection maps to the right shape.
// ---------------------------------------------------------------------------
static api::SessionSummary mk_sum(api::ThreadState st, api::ThreadTag tag,
                                  bool starred = false) {
    api::SessionSummary s;
    s.state = st;
    s.tag = tag;
    s.starred = starred;
    return s;
}

static void test_state_model_and_glyphs() {
    std::printf("test_state_model_and_glyphs\n");
    using namespace ecs::model;
    using api::ThreadState;
    using api::ThreadTag;

    // is_attention only true for Attention.
    CHECK(is_attention(ThreadState::Attention));
    CHECK(!is_attention(ThreadState::Ready));
    CHECK(!is_attention(ThreadState::Running));
    CHECK(!is_attention(ThreadState::Parked));
    CHECK(!is_attention(ThreadState::Archived));
    CHECK(!is_attention(ThreadState::Unknown));

    // Glyph precedence: Blocked>Review>Done, then bare Attention -> triangle.
    CHECK(glyph_for(mk_sum(ThreadState::Attention, ThreadTag::Blocked)) ==
          Glyph::Triangle);
    CHECK(glyph_for(mk_sum(ThreadState::Ready, ThreadTag::Review)) ==
          Glyph::Diamond);
    CHECK(glyph_for(mk_sum(ThreadState::Attention, ThreadTag::Done)) ==
          Glyph::Dot);
    // Bare attention (no tag) still earns the urgent triangle.
    CHECK(glyph_for(mk_sum(ThreadState::Attention, ThreadTag::None)) ==
          Glyph::Triangle);
    // Calm states carry no glyph.
    CHECK(glyph_for(mk_sum(ThreadState::Running, ThreadTag::None)) ==
          Glyph::None);
    CHECK(glyph_for(mk_sum(ThreadState::Parked, ThreadTag::None)) ==
          Glyph::None);
    CHECK(glyph_for(mk_sum(ThreadState::Archived, ThreadTag::None)) ==
          Glyph::None);

    // Precedence: a Blocked tag wins even on a non-Attention state.
    CHECK(glyph_for(mk_sum(ThreadState::Running, ThreadTag::Blocked)) ==
          Glyph::Triangle);

    // Spot-check the mock's actual seed classifies as intended.
    api::MockClient mm;
    auto r = mm.list_sessions();
    CHECK(r.ok);
    for (const auto& s : r.value) {
        if (s.id == "t1")  // blocked attention -> triangle
            CHECK(glyph_for(s) == Glyph::Triangle);
        if (s.id == "t4")  // ready/review -> diamond
            CHECK(glyph_for(s) == Glyph::Diamond);
        if (s.id == "t3")  // done tag -> dot
            CHECK(glyph_for(s) == Glyph::Dot);
        if (s.id == "t6")  // running -> none
            CHECK(glyph_for(s) == Glyph::None);
        if (s.id == "t10")  // calm / unknown -> none
            CHECK(glyph_for(s) == Glyph::None);
    }
}

// ---------------------------------------------------------------------------
// 3) Smart-view filtering: Blocked = tag==Blocked; Review = state==Ready
//    (agent-verified); Starred = starred. Counts match, over the real mock.
// ---------------------------------------------------------------------------
static void test_smart_view_filters() {
    std::printf("test_smart_view_filters\n");
    using namespace ecs::model;
    api::MockClient mm;
    auto r = mm.list_sessions();
    CHECK(r.ok);

    int blocked = 0, review = 0, starred = 0;
    for (const auto& s : r.value) {
        if (in_blocked_view(s)) ++blocked;
        if (in_review_view(s)) ++review;
        if (in_starred_view(s)) ++starred;
    }
    // The ported fixture has: 6 blocked (t1,r7,t2,r1,r2,r9), 3 review/ready
    // (t4,r4,t5), and no starred row at all — the fixture it mirrors has no
    // pinned field, so the Starred view is empty until the user stars one.
    CHECK(blocked == 6);
    CHECK(review == 3);
    CHECK(starred == 0);

    // Blocked view is exactly the Blocked-tagged rows (not just Attention).
    for (const auto& s : r.value)
        if (in_blocked_view(s)) CHECK(s.tag == api::ThreadTag::Blocked);
    // Review view is exactly agent-verified (Ready) rows.
    for (const auto& s : r.value)
        if (in_review_view(s)) CHECK(s.state == api::ThreadState::Ready);
}

// ---------------------------------------------------------------------------
// 4) Tab model: opening a thread adds a tab; opening an already-open thread
//    FOCUSES it (no duplicate); closing the active tab falls back correctly.
// ---------------------------------------------------------------------------
static ecs::AppComponent& setup_app_with_sessions() {
    reset_world();
    auto& appE = afterhours::EntityHelper::createEntity();
    auto& app = appE.addComponent<ecs::AppComponent>();
    api::MockClient m;
    app.sessions = m.list_sessions().value;  // so find_summary works for labels
    auto& stripE = afterhours::EntityHelper::createEntity();
    stripE.addComponent<ecs::TabStripComponent>();
    return app;
}

static ecs::TabStripComponent& the_strip() {
    // Mirror the app's find_singleton<> (EntityQuery-based), NOT the singleton
    // registry — the app registers these components via plain entities, not
    // registerSingleton, so get_singleton_cmp would return null here.
    auto q = afterhours::EntityQuery({.force_merge = true})
                 .whereHasComponent<ecs::TabStripComponent>()
                 .gen();
    return q[0].get().get<ecs::TabStripComponent>();
}

static void test_tab_open_focus_no_duplicate() {
    std::printf("test_tab_open_focus_no_duplicate\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    ecs::model::open_session_in_tab(strip, app, "t1");
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == "t1");
    CHECK(app.view == ecs::SmartView::Chat);
    CHECK(app.requestOpenId == "t1");
    // Label comes from the summary title.
    {
        auto* e = ecs::model::active_tab_entity();
        CHECK(e != nullptr);
        CHECK(e->get<ecs::Tab>().label ==
              "stickers broke \xe2\x80\x94 concluded, D113637134 on you");
    }

    ecs::model::open_session_in_tab(strip, app, "t4");
    CHECK(strip.tabOrder.size() == 2);
    CHECK(app.selectedId == "t4");

    // Re-opening t1 must FOCUS the existing tab, not create a duplicate.
    ecs::model::open_session_in_tab(strip, app, "t1");
    CHECK(strip.tabOrder.size() == 2);  // still two tabs
    CHECK(app.selectedId == "t1");
    // Exactly one tab is active.
    int active = 0;
    for (auto id : strip.tabOrder) {
        auto o = afterhours::EntityHelper::getEntityForID(id);
        if (o.valid() && o->has<ecs::ActiveTab>()) ++active;
    }
    CHECK(active == 1);
}

static void test_tab_preview_keeps_one_slot() {
    std::printf("test_tab_preview_keeps_one_slot\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    auto tab_at = [&](size_t i) -> ecs::Tab& {
        // Force the pending-entity merge the app gets for free every frame:
        // a tab created by the call just above is not in the entity map until
        // some query merges it, and active_tab_entity's does.
        (void)ecs::model::active_tab_entity();
        auto o = afterhours::EntityHelper::getEntityForID(strip.tabOrder[i]);
        return o.asE().get<ecs::Tab>();
    };

    // A sidebar row clicked once is a preview.
    ecs::model::open_session_in_tab(strip, app, "t1", /*keep=*/false);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(!tab_at(0).keptOpen);
    CHECK(app.selectedId == "t1");

    // Clicking down a list does not leave a trail: the second glance REUSES the
    // preview tab — same slot, same entity, new thread.
    const auto firstId = strip.tabOrder[0];
    ecs::model::open_session_in_tab(strip, app, "t4", /*keep=*/false);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(strip.tabOrder[0] == firstId);
    CHECK(tab_at(0).sessionId == "t4");
    CHECK(!tab_at(0).keptOpen);
    CHECK(tab_at(0).label == "finished, and wants you to read it");

    // Asking for the thread you are already looking at is the second look, and
    // the second look keeps it.
    ecs::model::open_session_in_tab(strip, app, "t4", /*keep=*/false);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(tab_at(0).keptOpen);

    // Now a kept tab is in the way, so the next preview gets its own slot
    // beside it rather than replacing it.
    ecs::model::open_session_in_tab(strip, app, "t1", /*keep=*/false);
    CHECK(strip.tabOrder.size() == 2);
    CHECK(tab_at(0).sessionId == "t4" && tab_at(0).keptOpen);
    CHECK(tab_at(1).sessionId == "t1" && !tab_at(1).keptOpen);
    CHECK(ecs::model::preview_tab_entity(strip) != nullptr);
    CHECK(ecs::model::preview_tab_entity(strip)->id == strip.tabOrder[1]);

    // And that one is replaced in turn, so there is never more than one.
    ecs::model::open_session_in_tab(strip, app, "t6", /*keep=*/false);
    CHECK(strip.tabOrder.size() == 2);
    CHECK(tab_at(1).sessionId == "t6");

    // An explicit open (restore, kickoff, "open in split") is a commitment, so
    // it defaults to kept and never eats the preview.
    ecs::model::open_session_in_tab(strip, app, "t13");
    CHECK(strip.tabOrder.size() == 3);
    CHECK(tab_at(2).keptOpen);
    CHECK(tab_at(1).sessionId == "t6" && !tab_at(1).keptOpen);

    // Keeping is idempotent, so every path that means it can just say it.
    auto& prev = *ecs::model::preview_tab_entity(strip);
    ecs::model::keep_tab(prev);
    ecs::model::keep_tab(prev);
    CHECK(prev.get<ecs::Tab>().keptOpen);
    CHECK(ecs::model::preview_tab_entity(strip) == nullptr);
}

// KICKOFF from the Home landing composer: the loader creates a brand-new
// open_session_in_tab. That MUST still create a tab + switch the view to Chat
// even though no summary exists yet (the list refresh lands a frame later).
// This is the regression guard for "no chat input" -> Home landing composer.
static void test_kickoff_opens_new_tab_without_summary() {
    std::printf("test_kickoff_opens_new_tab_without_summary\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();
    app.view = ecs::SmartView::Home;

    const std::string freshId = "brand-new-kickoff-id";
    CHECK(app.find_summary(freshId) == nullptr);  // not in the list yet

    ecs::model::open_session_in_tab(strip, app, freshId);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == freshId);
    CHECK(app.view == ecs::SmartView::Chat);     // Home -> Chat transition
    CHECK(app.requestOpenId == freshId);         // loader fetches the transcript
    // With no summary, the label falls back to the id (no crash / empty label).
    {
        auto* e = ecs::model::active_tab_entity();
        CHECK(e != nullptr);
        CHECK(e->get<ecs::Tab>().label == freshId);
    }
}

static void test_tab_close_fallback() {
    std::printf("test_tab_close_fallback\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    ecs::model::open_session_in_tab(strip, app, "t1");
    ecs::model::open_session_in_tab(strip, app, "t4");
    ecs::model::open_session_in_tab(strip, app, "t5");
    CHECK(strip.tabOrder.size() == 3);
    CHECK(app.selectedId == "t5");  // last opened is active

    // Close the active (last) tab -> fall back to the neighbor (min(idx,size-1)).
    auto activeId = ecs::model::active_tab_entity()->id;
    // find its index
    size_t idx = 0;
    for (size_t i = 0; i < strip.tabOrder.size(); ++i)
        if (strip.tabOrder[i] == activeId) idx = i;
    ecs::model::close_tab(strip, app, activeId, idx, /*wasActive=*/true);
    CHECK(strip.tabOrder.size() == 2);
    CHECK(app.selectedId == "t4");  // fell back to previous tab

    // Close a non-active tab (t1 at index 0) while t4 active -> t4 stays.
    auto firstId = strip.tabOrder[0];
    bool firstWasActive = false;
    {
        auto o = afterhours::EntityHelper::getEntityForID(firstId);
        firstWasActive = o.valid() && o->has<ecs::ActiveTab>();
    }
    ecs::model::close_tab(strip, app, firstId, 0, firstWasActive);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == "t4");

    // Close the last remaining tab -> back to Home digest, no open transcript.
    auto lastId = strip.tabOrder[0];
    ecs::model::close_tab(strip, app, lastId, 0, /*wasActive=*/true);
    CHECK(strip.tabOrder.empty());
    CHECK(app.selectedId.empty());
    CHECK(app.view == ecs::SmartView::Home);
    CHECK(!app.openSession.has_value());
}

// Explicit tab SWITCHING (Gabe: "make sure switching tabs works"): with several
// tabs open, switching to any one must (a) keep EXACTLY one ActiveTab marker,
// (b) set selectedId to that tab, (c) set requestOpenId so the loader reloads
// the transcript, and (d) set the Chat view — across repeated back-and-forth
// switches with no leak/duplicate.
static void test_tab_switch_between_open_tabs() {
    std::printf("test_tab_switch_between_open_tabs\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();
    ecs::model::open_session_in_tab(strip, app, "t1");
    ecs::model::open_session_in_tab(strip, app, "t4");
    ecs::model::open_session_in_tab(strip, app, "t5");
    CHECK(strip.tabOrder.size() == 3);

    auto tab_for = [&](const std::string& sid) -> afterhours::Entity* {
        for (auto id : strip.tabOrder) {
            auto o = afterhours::EntityHelper::getEntityForID(id);
            if (o.valid() && o->has<ecs::Tab>() &&
                o->get<ecs::Tab>().sessionId == sid)
                return &o.asE();
        }
        return nullptr;
    };
    auto active_count = [&]() {
        int n = 0;
        for (auto id : strip.tabOrder) {
            auto o = afterhours::EntityHelper::getEntityForID(id);
            if (o.valid() && o->has<ecs::ActiveTab>()) ++n;
        }
        return n;
    };

    // Switch to each open tab in turn (including back-and-forth) and assert the
    // invariants hold every time.
    for (const char* target : {"t1", "t5", "t4", "t1", "t4"}) {
        auto* e = tab_for(target);
        CHECK(e != nullptr);
        if (!e) continue;
        app.requestOpenId.clear();  // prove the switch sets it fresh
        ecs::model::switch_to_tab(app, *e);
        CHECK(app.selectedId == target);
        CHECK(app.requestOpenId == target);  // triggers transcript reload
        CHECK(app.view == ecs::SmartView::Chat);
        CHECK(active_count() == 1);  // exactly one active, no leak/dup
        CHECK(strip.tabOrder.size() == 3);  // switching never adds/removes tabs
    }
}

// ---------------------------------------------------------------------------
// 4d) Tab REORDER (drag-to-rearrange): the drop-index geometry and the
//     erase+insert reorder are PURE (model::compute_drop_index /
//     model::reorder_tab), so we exercise the "drag tab i, drop at j" contract
//     deterministically without a live mouse. The render/input header only
//     computes a draggedCenterX and hands it to these same functions, so the
//     tested logic is the shipped logic.
// ---------------------------------------------------------------------------
static void test_tab_reorder_drop_index() {
    std::printf("test_tab_reorder_drop_index\n");
    // 4 slots, first slot left edge at x=100, each slot 100px wide (stride).
    const float stripX = 100.0f;
    const float stride = 100.0f;
    const size_t n = 4;  // slot centers: 150, 250, 350, 450

    // A center sitting squarely over each slot maps to that slot's index.
    CHECK(ecs::model::compute_drop_index(150.0f, stripX, stride, n) == 0);
    CHECK(ecs::model::compute_drop_index(250.0f, stripX, stride, n) == 1);
    CHECK(ecs::model::compute_drop_index(350.0f, stripX, stride, n) == 2);
    CHECK(ecs::model::compute_drop_index(450.0f, stripX, stride, n) == 3);

    // Past the ends clamps to first / last.
    CHECK(ecs::model::compute_drop_index(-999.0f, stripX, stride, n) == 0);
    CHECK(ecs::model::compute_drop_index(9999.0f, stripX, stride, n) == 3);

    // Dragging left over slot 0's band -> 0; over slot 1's band -> 1.
    CHECK(ecs::model::compute_drop_index(120.0f, stripX, stride, n) == 0);
    CHECK(ecs::model::compute_drop_index(230.0f, stripX, stride, n) == 1);

    // Single tab (or zero stride) -> no meaningful drop; index 0.
    CHECK(ecs::model::compute_drop_index(500.0f, stripX, stride, 1) == 0);
    CHECK(ecs::model::compute_drop_index(500.0f, stripX, 0.0f, n) == 0);
}

// Assert the shipped erase+insert reorder against explicit before/after orders,
// AND that reordering never disturbs which tab is active (order-only).
static void test_tab_reorder_moves_and_preserves_active() {
    std::printf("test_tab_reorder_moves_and_preserves_active\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    ecs::model::open_session_in_tab(strip, app, "t1");  // idx 0
    ecs::model::open_session_in_tab(strip, app, "t4");  // idx 1
    ecs::model::open_session_in_tab(strip, app, "t5");  // idx 2 (active)
    CHECK(strip.tabOrder.size() == 3);

    // Record the active tab entity + its session before any reorder.
    auto* activeBefore = ecs::model::active_tab_entity();
    CHECK(activeBefore != nullptr);
    auto activeId = activeBefore->id;
    std::string activeSid = activeBefore->get<ecs::Tab>().sessionId;  // "t5"
    std::string selectedBefore = app.selectedId;

    auto sid_at = [&](size_t i) {
        auto o = afterhours::EntityHelper::getEntityForID(strip.tabOrder[i]);
        return o->get<ecs::Tab>().sessionId;
    };

    // Drag the first tab (t1, idx 0) and drop it at the end (idx 2).
    ecs::model::reorder_tab(strip, 0, 2);
    CHECK(strip.tabOrder.size() == 3);  // no add/remove
    CHECK(sid_at(0) == "t4");
    CHECK(sid_at(1) == "t5");
    CHECK(sid_at(2) == "t1");

    // Drag the last tab (t1, idx 2) back to the front (idx 0).
    ecs::model::reorder_tab(strip, 2, 0);
    CHECK(sid_at(0) == "t1");
    CHECK(sid_at(1) == "t4");
    CHECK(sid_at(2) == "t5");

    // Middle move: idx 1 -> idx 0.
    ecs::model::reorder_tab(strip, 1, 0);
    CHECK(sid_at(0) == "t4");
    CHECK(sid_at(1) == "t1");
    CHECK(sid_at(2) == "t5");

    // The ACTIVE tab and app selection are untouched by reordering.
    auto* activeAfter = ecs::model::active_tab_entity();
    CHECK(activeAfter != nullptr);
    CHECK(activeAfter->id == activeId);
    CHECK(activeAfter->get<ecs::Tab>().sessionId == activeSid);  // still "t5"
    CHECK(app.selectedId == selectedBefore);
    // Exactly one active tab still.
    int active = 0;
    for (auto id : strip.tabOrder) {
        auto o = afterhours::EntityHelper::getEntityForID(id);
        if (o.valid() && o->has<ecs::ActiveTab>()) ++active;
    }
    CHECK(active == 1);
}

// Edge cases: no-op reorders must not corrupt the order (single tab, equal
// from/to, out-of-range indices).
static void test_tab_reorder_edge_cases() {
    std::printf("test_tab_reorder_edge_cases\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    // Single tab -> dragging is a no-op.
    ecs::model::open_session_in_tab(strip, app, "t1");
    ecs::model::reorder_tab(strip, 0, 0);
    CHECK(strip.tabOrder.size() == 1);

    ecs::model::open_session_in_tab(strip, app, "t4");
    ecs::model::open_session_in_tab(strip, app, "t5");
    auto snapshot = strip.tabOrder;  // [t1, t4, t5]

    ecs::model::reorder_tab(strip, 1, 1);          // equal -> no-op
    CHECK(strip.tabOrder == snapshot);
    ecs::model::reorder_tab(strip, 99, 0);         // from OOR -> no-op
    CHECK(strip.tabOrder == snapshot);
    ecs::model::reorder_tab(strip, 0, 99);         // to OOR -> no-op
    CHECK(strip.tabOrder == snapshot);
}

// ---------------------------------------------------------------------------
// Chrome-style overflow width + horizontal scroll math (pure model). These are
// the SHIPPED functions the tab bar uses to size tabs and clamp the scroll
// offset, exercised directly (no window / no mouse).
// ---------------------------------------------------------------------------
static void test_tab_overflow_width() {
    std::printf("test_tab_overflow_width\n");
    const float minW = 40.0f;
    const float maxW = 240.0f;
    const float gap = 3.0f;

    // A single tab in a wide strip is capped at maxW (doesn't stretch huge).
    CHECK(ecs::model::compute_tab_width(1200.0f, 1, minW, maxW, gap) == maxW);
    // Two tabs still capped (2*240 + gap < 1200).
    CHECK(ecs::model::compute_tab_width(1200.0f, 2, minW, maxW, gap) == maxW);

    // Enough tabs that the uniform share drops below maxW but stays above minW:
    // 10 tabs in 1000px -> ~(1000 - 27)/10 = 97.3px, between min and max.
    float w10 = ecs::model::compute_tab_width(1000.0f, 10, minW, maxW, gap);
    CHECK(w10 > minW && w10 < maxW);
    CHECK(std::fabs(w10 - (1000.0f - gap * 9.0f) / 10.0f) < 0.01f);

    // Many tabs: the share would go below minW, so it CLAMPS at minW (Chrome
    // stops shrinking and scrolls instead). 40 tabs in 300px -> way under min.
    CHECK(ecs::model::compute_tab_width(300.0f, 40, minW, maxW, gap) == minW);

    // Zero tabs -> maxW (well-defined, unused).
    CHECK(ecs::model::compute_tab_width(500.0f, 0, minW, maxW, gap) == maxW);
}

static void test_tab_scroll_clamp_and_max() {
    std::printf("test_tab_scroll_clamp_and_max\n");
    const float gap = 3.0f;
    const float tabW = 40.0f;

    // Content that fits -> no scroll possible (maxScroll == 0), any offset
    // clamps to 0.
    float fitMax = ecs::model::compute_max_scroll(1000.0f, 3, tabW, gap);
    CHECK(fitMax == 0.0f);
    CHECK(ecs::model::clamp_scroll(50.0f, fitMax) == 0.0f);
    CHECK(ecs::model::clamp_scroll(-50.0f, fitMax) == 0.0f);

    // Content overflows: 20 tabs @ 40px + 19 gaps = 800 + 57 = 857; strip 300
    // -> maxScroll 557.
    float ovMax = ecs::model::compute_max_scroll(300.0f, 20, tabW, gap);
    CHECK(std::fabs(ovMax - (857.0f - 300.0f)) < 0.01f);
    // Clamp respects [0, maxScroll].
    CHECK(ecs::model::clamp_scroll(-10.0f, ovMax) == 0.0f);
    CHECK(ecs::model::clamp_scroll(9999.0f, ovMax) == ovMax);
    CHECK(ecs::model::clamp_scroll(100.0f, ovMax) == 100.0f);
}

static void test_tab_scroll_to_show_active() {
    std::printf("test_tab_scroll_to_show_active\n");
    const float gap = 3.0f;
    const float tabW = 40.0f;
    const float stripW = 200.0f;   // fits ~4-5 tabs
    const size_t n = 20;           // overflows
    const float stride = tabW + gap;

    // Tab already fully visible at offset 0 (index 0) -> offset unchanged (0).
    CHECK(ecs::model::scroll_to_show(0, 0.0f, stripW, tabW, gap, n) == 0.0f);

    // A far-right tab (index 19): its left edge = 19*43 = 817, right = 857.
    // From offset 0 it's off-screen-right, so scroll so its right aligns to the
    // strip's right: 857 - 200 = 657, clamped to maxScroll.
    float maxScroll = ecs::model::compute_max_scroll(stripW, n, tabW, gap);
    float toRight = ecs::model::scroll_to_show(19, 0.0f, stripW, tabW, gap, n);
    CHECK(std::fabs(toRight - (817.0f + tabW - stripW)) < 0.01f);
    CHECK(toRight <= maxScroll + 0.01f);

    // A tab to the LEFT of the current viewport scrolls left to reveal its left
    // edge. Start scrolled far right, then show index 0 -> offset becomes its
    // left edge (0).
    CHECK(ecs::model::scroll_to_show(0, maxScroll, stripW, tabW, gap, n) ==
          0.0f);

    // A tab already inside the viewport keeps the current offset. At offset
    // 100, index 3 sits at [129,169] within [100,300] -> unchanged.
    (void)stride;
    CHECK(ecs::model::scroll_to_show(3, 100.0f, stripW, tabW, gap, n) ==
          100.0f);
}

// close_others: closes every tab except the right-clicked one, keeps it active,
// and its content stays open. Mirrors the tab context-menu action.
static void test_tab_close_others() {
    std::printf("test_tab_close_others\n");
    auto& app = setup_app_with_sessions();
    auto& strip = the_strip();

    ecs::model::open_session_in_tab(strip, app, "t1");  // idx 0
    ecs::model::open_session_in_tab(strip, app, "t4");  // idx 1
    ecs::model::open_session_in_tab(strip, app, "t5");  // idx 2 (active)
    CHECK(strip.tabOrder.size() == 3);

    // Keep the MIDDLE (non-active) tab t4; the others (t1, t5) close.
    ecs::model::close_others(strip, app, "t4");
    CHECK(strip.tabOrder.size() == 1);
    auto keptOpt =
        afterhours::EntityHelper::getEntityForID(strip.tabOrder[0]);
    CHECK(keptOpt.valid());
    CHECK(keptOpt->get<ecs::Tab>().sessionId == "t4");
    // The kept tab becomes active and its content is open.
    CHECK(keptOpt->has<ecs::ActiveTab>());
    CHECK(app.selectedId == "t4");
    CHECK(app.view == ecs::SmartView::Chat);
    // Exactly one active tab.
    int active = 0;
    for (auto id : strip.tabOrder) {
        auto o = afterhours::EntityHelper::getEntityForID(id);
        if (o.valid() && o->has<ecs::ActiveTab>()) ++active;
    }
    CHECK(active == 1);

    // Closing others when only the kept tab remains is a stable no-op.
    ecs::model::close_others(strip, app, "t4");
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == "t4");

    // Keeping a tab that isn't open is a no-op (order unchanged).
    ecs::model::close_others(strip, app, "does-not-exist");
    CHECK(strip.tabOrder.size() == 1);
    CHECK(strip.tabOrder[0] == keptOpt->id);
}

// The web URL shape the "Copy URL" action copies. Default is host-neutral
// (no hardcoded web host); a configured web base is joined without doubling '/'.
static void test_navi_url_shape() {
    std::printf("test_navi_url_shape\n");
    // Unconfigured => host-neutral scheme, no company/host baked in.
    CHECK(ecs::model::navi_url_for("", "t5") == "navi://session/t5");
    CHECK(ecs::model::navi_url_for("", "") == "navi://session/");
    // Configured base (any origin the operator sets) is joined cleanly.
    CHECK(ecs::model::navi_url_for("https://example.test", "t5") ==
          "https://example.test/t5");
    CHECK(ecs::model::navi_url_for("https://example.test/", "abc-123") ==
          "https://example.test/abc-123");
}

// ---------------------------------------------------------------------------
// 5) http adapter defaults: with no env config, make_client() returns the

//    mock; a default summary (what the generic http adapter yields) is calm.
// ---------------------------------------------------------------------------
static void test_backend_agnostic_defaults() {
    std::printf("test_backend_agnostic_defaults\n");
    unsetenv("HANABI_BACKEND");
    unsetenv("HANABI_BASE_URL");
    unsetenv("HANABI_API_BASE_URL");
    unsetenv("HANABI_TOKEN");
    // Isolate from any real ~/.config/hanabi/config.json on this machine: point
    // HANABI_CONFIG at a path that cannot exist so from_env() genuinely sees
    // "no config file" and the zero-config mock default is what's under test
    // (otherwise a developer's real http config.json would shadow the default).
    setenv("HANABI_CONFIG", "/nonexistent/hanabi/config.json.test", 1);
    api::Config c = api::Config::from_env();
    CHECK(c.backend == "mock");
    CHECK(!c.http_ready());
    auto client = api::make_client(c);
    CHECK(client != nullptr);
    CHECK(client->backend_label() == "mock");

    // Requesting http with no base url still falls back to mock (no crash).
    api::Config h;
    h.backend = "http";
    h.base_url = "";
    auto hc = api::make_client(h);
    CHECK(hc != nullptr);
    CHECK(hc->backend_label() == "mock");

    // A default summary (the generic http adapter's degraded output) is calm:
    // Unknown state, no tag, not starred, no folder.
    api::SessionSummary def;
    CHECK(def.state == api::ThreadState::Unknown);
    CHECK(def.tag == api::ThreadTag::None);
    CHECK(!def.starred);
    CHECK(def.folder.empty());
    // And it carries NO status glyph (stays calm).
    CHECK(ecs::model::glyph_for(def) == ecs::model::Glyph::None);
}

// ---------------------------------------------------------------------------
// 6) Transcript LRU cache (Phase X). The cache (last 20 msgs x last 5 threads)
//    lives in the app layer behind api::Client so mock + http both benefit.
//    We assert the SHIPPED decision: on a cache HIT the transcript is served
//    synchronously and NO get_session fetch fires; on a MISS the fetch runs
//    and the result is inserted (capped to 20 msgs); the 6th distinct thread
//    evicts the LRU; re-opening an evicted thread re-fetches.
//
//    Fetch-counting is done with an instrumented Client wrapper. The loader's
//    real resolution logic is mirrored by resolve_transcript() below, which
//    calls the SAME TranscriptCache methods the LoaderSystem calls (cache.get
//    on open, cache.put on a miss's result) — so the tested logic IS the
//    shipped logic. (The loader wraps the miss fetch in std::async; here we run
//    it synchronously to keep the test deterministic and headless.)
// ---------------------------------------------------------------------------

// Instrumented client: counts get_session calls to prove cache hits avoid a
// fetch. Delegates to the mock for the actual (deterministic) data.
struct CountingClient : api::Client {
    api::MockClient inner;
    int getSessionCalls = 0;

    std::string backend_label() const override { return inner.backend_label(); }
    api::Result<std::vector<api::SessionSummary>> list_sessions() override {
        return inner.list_sessions();
    }
    api::Result<api::Session> get_session(const std::string& id) override {
        ++getSessionCalls;
        return inner.get_session(id);
    }
};

// Mirrors LoaderSystem's transcript resolution: cache HIT -> serve
// synchronously (no fetch); MISS -> fetch + insert (capped). Returns true on a
// cache hit (i.e. NO fetch happened).
static bool resolve_transcript(ecs::AppComponent& app, api::Client& client,
                               const std::string& id) {
    app.selectedId = id;
    if (auto hit = app.transcriptCache.get(id)) {
        app.openSession = std::move(*hit);
        app.transcriptState = ecs::LoadState::Loaded;
        return true;  // synchronous hit, no fetch
    }
    auto r = client.get_session(id);
    if (r.ok) {
        app.transcriptCache.put(r.value);
        app.openSession = std::move(r.value);
        app.transcriptState = ecs::LoadState::Loaded;
    } else {
        app.openSession.reset();
        app.transcriptState = ecs::LoadState::Error;
    }
    return false;  // miss -> fetched
}

static void test_transcript_cache() {
    std::printf("test_transcript_cache\n");
    reset_world();
    auto& appE = afterhours::EntityHelper::createEntity();
    auto& app = appE.addComponent<ecs::AppComponent>();
    CountingClient client;

    // (a) First open of t1 is a MISS -> fetches once, serves + caches.
    bool hit = resolve_transcript(app, client, "t1");
    CHECK(!hit);
    CHECK(client.getSessionCalls == 1);
    CHECK(app.openSession.has_value());
    CHECK(app.transcriptState == ecs::LoadState::Loaded);
    CHECK(app.transcriptCache.contains("t1"));

    // (a) Re-opening t1 is a cache HIT -> served SYNCHRONOUSLY, NO new fetch.
    hit = resolve_transcript(app, client, "t1");
    CHECK(hit);
    CHECK(client.getSessionCalls == 1);  // unchanged: no fetch on a hit
    CHECK(app.openSession.has_value());
    CHECK(app.openSession->summary.id == "t1");

    // (b) Cached transcript is capped at 20 messages (most-recent).
    {
        auto cached = app.transcriptCache.get("t1");
        CHECK(cached.has_value());
        CHECK(cached->messages.size() <= ecs::model::kCacheMaxMessagesPerThread);
        // And the cap is over the LAST 20: verify the tail matches the source.
        auto full = client.inner.get_session("t1");
        CHECK(full.ok);
        if (full.value.messages.size() > ecs::model::kCacheMaxMessagesPerThread) {
            CHECK(cached->messages.size() == ecs::model::kCacheMaxMessagesPerThread);
            CHECK(cached->messages.back().id == full.value.messages.back().id);
        }
    }

    // Cap is genuinely enforced even for an oversized synthetic transcript.
    {
        api::Session big;
        big.summary.id = "big";
        for (int i = 0; i < 50; ++i) {
            api::Message m;
            m.id = "bm" + std::to_string(i);
            big.messages.push_back(m);
        }
        app.transcriptCache.put(big);
        auto got = app.transcriptCache.get("big");
        CHECK(got.has_value());
        CHECK(got->messages.size() == ecs::model::kCacheMaxMessagesPerThread);
        CHECK(got->messages.back().id == "bm49");   // kept the newest
        CHECK(got->messages.front().id == "bm30");  // dropped the oldest 30
    }

    // Fresh app for the eviction test so recency is well-defined.
    reset_world();
    auto& app2E = afterhours::EntityHelper::createEntity();
    auto& app2 = app2E.addComponent<ecs::AppComponent>();
    CountingClient client2;

    // (c) Open 5 distinct threads -> all cached, one fetch each.
    const char* five[] = {"t1", "t2", "t3", "t4", "t5"};
    for (const auto* id : five) CHECK(!resolve_transcript(app2, client2, id));
    CHECK(client2.getSessionCalls == 5);
    CHECK(app2.transcriptCache.size() == ecs::model::kCacheMaxThreads);

    // Touch t1 so it becomes most-recent; t2 is now the LRU.
    CHECK(resolve_transcript(app2, client2, "t1"));  // hit, no fetch
    CHECK(client2.getSessionCalls == 5);

    // Opening a 6th distinct thread evicts the LRU (t2), still <=5 threads.
    CHECK(!resolve_transcript(app2, client2, "t6"));
    CHECK(client2.getSessionCalls == 6);
    CHECK(app2.transcriptCache.size() == ecs::model::kCacheMaxThreads);
    CHECK(!app2.transcriptCache.contains("t2"));  // evicted (was LRU)
    CHECK(app2.transcriptCache.contains("t1"));   // touched -> retained
    CHECK(app2.transcriptCache.contains("t6"));   // newest

    // (d) Re-opening the evicted thread (t2) RE-FETCHES (async path intact).
    CHECK(!resolve_transcript(app2, client2, "t2"));  // miss -> fetch
    CHECK(client2.getSessionCalls == 7);
    CHECK(app2.transcriptCache.contains("t2"));
}

// ---------------------------------------------------------------------------
// Sidebar scroll list — text/rows must render at ALL scroll offsets (bug fix).
//
// BUG: the sidebar thread list stopped drawing row text/rows past a certain
// scroll offset (rows stayed clickable but invisible). ROOT CAUSE: the sidebar
// scroll panel is a FlexDirection::Column, and preset::ScrollPanel left
// flex_wrap at its afterhours DEFAULT of FlexWrap::Wrap. The sidebar puts MANY
// direct children in that panel (folder heads + 90+ chat rows); once their
// stacked height exceeds the viewport, a WRAPPING column wraps the overflow
// into a SECOND column at x += column-width — i.e. off the right edge of the
// sidebar, where the scroll viewport's scissor clips it away. So only the first
// viewport-height of rows ever drew; everything past content-Y ≈ viewport
// height laid out (and stayed hit-testable) in a clipped-out column, looking
// "empty" once scrolled. FIX: force FlexWrap::NoWrap on the sidebar scroll
// panel so every child stacks in ONE column and the scroll offset simply slides
// the whole list.
//
// This test reproduces the layout at the engine level (no graphics needed —
// autolayout is pure geometry): it builds a Column scroll panel with far more
// tall rows than fit the viewport, runs afterhours' real autolayout, and
// asserts EVERY row stacks in a single column (same computed X, monotonically
// increasing Y). Under the old Wrap default this fails — later rows get a
// different (wrapped) X and a Y that resets — which is exactly the invisible-
// row bug. It then checks that at a non-zero scroll offset, the rows whose
// scroll-adjusted Y lands inside the viewport have valid on-screen positions
// (i.e. they'd be drawn), proving text renders when scrolled.
// ---------------------------------------------------------------------------
static void test_sidebar_row_manual_order() {
    std::printf("-- sidebar manual row order (drag-to-reorder) --\n");

    // Four threads in activity order, newest first — what the sidebar shows
    // with nobody having arranged anything.
    api::SessionSummary a, b, c, d;
    a.id = "a"; b.id = "b"; c.id = "c"; d.id = "d";
    std::vector<const api::SessionSummary*> rows{&a, &b, &c, &d};

    // No manual order: activity order is left exactly as it was.
    ecs::model::apply_row_order(rows, {});
    CHECK(rows[0]->id == "a" && rows[3]->id == "d");

    // A manual order is a PINNED PREFIX, not a re-sort: the named rows come
    // first in the order given, and the rest keep flowing in activity order.
    ecs::model::apply_row_order(rows, {"c", "a"});
    CHECK(rows[0]->id == "c");
    CHECK(rows[1]->id == "a");
    CHECK(rows[2]->id == "b");
    CHECK(rows[3]->id == "d");

    // An order naming a thread that is no longer in this folder is not a
    // problem — it simply pins nothing.
    std::vector<const api::SessionSummary*> rows2{&a, &b};
    ecs::model::apply_row_order(rows2, {"zzz", "b"});
    CHECK(rows2[0]->id == "b");
    CHECK(rows2[1]->id == "a");

    // The drop slot is read off the rendered band's geometry: first row at
    // y=100, 24px rows. Above the band clamps to the first slot, below to the
    // last, and a cursor inside row 2's box lands on slot 2.
    CHECK(ecs::model::compute_row_drop_index(100.0f, 100.0f, 24.0f, 5) == 0);
    CHECK(ecs::model::compute_row_drop_index(60.0f, 100.0f, 24.0f, 5) == 0);
    CHECK(ecs::model::compute_row_drop_index(155.0f, 100.0f, 24.0f, 5) == 2);
    CHECK(ecs::model::compute_row_drop_index(9000.0f, 100.0f, 24.0f, 5) == 4);
    // Degenerate inputs answer rather than divide by zero.
    CHECK(ecs::model::compute_row_drop_index(50.0f, 100.0f, 0.0f, 5) == 0);
    CHECK(ecs::model::compute_row_drop_index(50.0f, 100.0f, 24.0f, 1) == 0);

    // A drop rewrites the prefix: move the last row to the front, to the
    // middle, and onto itself.
    const std::vector<std::string> vis{"a", "b", "c", "d"};
    auto moved = ecs::model::reorder_rows(vis, "d", 0);
    CHECK(moved.size() == 4);
    CHECK(moved[0] == "d" && moved[1] == "a" && moved[2] == "b" &&
          moved[3] == "c");
    auto mid = ecs::model::reorder_rows(vis, "a", 2);
    CHECK(mid[0] == "b" && mid[1] == "c" && mid[2] == "a" && mid[3] == "d");
    auto same = ecs::model::reorder_rows(vis, "b", 1);
    CHECK(same[0] == "a" && same[1] == "b" && same[2] == "c" &&
          same[3] == "d");
    // A row that is not one of these leaves the order alone.
    auto untouched = ecs::model::reorder_rows(vis, "zzz", 0);
    CHECK(untouched == vis);

    // The persisted prefix is bounded: a folder with far more rows than the cap
    // still writes at most kRowOrderMax ids, so a 2000-session folder costs the
    // same to remember as a 20-session one.
    std::vector<std::string> many;
    for (int i = 0; i < 500; ++i) many.push_back("s" + std::to_string(i));
    auto capped = ecs::model::reorder_rows(many, "s400", 0);
    CHECK(capped.size() == ecs::model::kRowOrderMax);
    CHECK(capped[0] == "s400");
    CHECK(capped[1] == "s0");
}

static void test_sidebar_scroll_list_single_column() {
    std::printf("test_sidebar_scroll_list_single_column\n");
    using namespace afterhours;
    using namespace afterhours::ui;

    reset_world();

    // Build the scroll panel + N rows as bare UIComponents (what preset +
    // render_chat_row produce structurally: a Column panel holding many
    // fixed-height row children).
    const float kPanelW = 280.f;
    const float kViewportH = 400.f;
    const float kRowH = 24.f;
    const int kRows = 60;  // 60*24 = 1440px >> 400px viewport (forces overflow)

    auto& panelE = EntityHelper::createEntity();
    auto& panel = panelE.addComponent<UIComponent>(panelE.id);
    panel.desired[Axis::X] = pixels(kPanelW);
    panel.desired[Axis::Y] = pixels(kViewportH);
    panel.flex_direction = FlexDirection::Column;
    // THE FIX UNDER TEST: NoWrap keeps every row in one column. (Flip this to
    // FlexWrap::Wrap and the single-column assertions below fail — that's the
    // regression this guards.)
    panel.flex_wrap = FlexWrap::NoWrap;
    // Mark it a scroll view like the real preset::ScrollPanel — autolayout then
    // treats content overflow as expected (suppresses the overflow warn) so the
    // test's own layout doesn't spew warnings, matching the shipped widget.
    panelE.addComponent<HasScrollView>();

    std::vector<EntityID> rowIds;
    std::vector<Entity*> rowEnts;
    for (int i = 0; i < kRows; ++i) {
        auto& rowE = EntityHelper::createEntity();
        auto& row = rowE.addComponent<UIComponent>(rowE.id);
        row.desired[Axis::X] = pixels(kPanelW);
        row.desired[Axis::Y] = pixels(kRowH);
        row.flex_direction = FlexDirection::Row;
        row.flex_wrap = FlexWrap::NoWrap;
        row.parent = panelE.id;
        panel.children.push_back(rowE.id);
        rowIds.push_back(rowE.id);
        rowEnts.push_back(&rowE);
    }

    // Mapping vector indexed by entity id (what AutoLayout expects). Size it to
    // cover every id we created and point each slot at its entity.
    EntityID maxId = panelE.id;
    for (auto id : rowIds) maxId = std::max(maxId, id);
    std::vector<Entity*> mapping(static_cast<size_t>(maxId) + 1, nullptr);
    mapping[static_cast<size_t>(panelE.id)] = &panelE;
    for (size_t i = 0; i < rowIds.size(); ++i)
        mapping[static_cast<size_t>(rowIds[i])] = rowEnts[i];

    window_manager::Resolution rez;
    rez.width = 1100;
    rez.height = 760;
    AutoLayout::autolayout(panel, rez, mapping);

    // (1) SINGLE COLUMN: every row shares the panel's X and never wraps into a
    //     second column. This is the property the NoWrap fix guarantees and the
    //     old Wrap default broke (wrapped rows got x += column-width and were
    //     scissored out — the invisible-rows-when-scrolled bug).
    const float rowX0 = mapping[static_cast<size_t>(rowIds[0])]
                            ->get<UIComponent>()
                            .computed_rel[Axis::X];
    float prevY = -1.f;
    bool allSameX = true;
    bool yMonotonic = true;
    for (auto id : rowIds) {
        const auto& rc = mapping[static_cast<size_t>(id)]->get<UIComponent>();
        if (std::abs(rc.computed_rel[Axis::X] - rowX0) > 0.5f) allSameX = false;
        if (rc.computed_rel[Axis::Y] <= prevY) yMonotonic = false;
        prevY = rc.computed_rel[Axis::Y];
    }
    CHECK(allSameX);     // no wrapped-off-to-the-right column
    CHECK(yMonotonic);   // rows stack straight down, newest math preserved

    // The last row's Y must extend WELL past the viewport (content overflows),
    // proving the rows past the fold are laid out in-column (not piled at the
    // viewport bottom). Old Wrap behavior capped this near the viewport height.
    const float lastY = mapping[static_cast<size_t>(rowIds[kRows - 1])]
                            ->get<UIComponent>()
                            .computed_rel[Axis::Y];
    CHECK(lastY > kViewportH);           // content really overflows
    CHECK(lastY > kViewportH * 2.0f);    // and the tail rows sit far down, in-column

    // (2) RENDERS WHEN SCROLLED: at a non-zero scroll offset, a row whose
    //     content-Y falls within [offset, offset+viewport] has a scroll-
    //     adjusted on-screen Y inside the viewport, so its label WOULD be
    //     drawn. (Render subtracts the scroll offset: screenY = Y - offset.)
    //     Pick an offset deep into the list — the exact spot the old bug went
    //     blank — and assert at least a viewport-worth of rows are on-screen.
    const float scrollOffset = 600.f;  // ~row 25; past the old cutoff
    int onScreenRows = 0;
    for (auto id : rowIds) {
        const auto& rc = mapping[static_cast<size_t>(id)]->get<UIComponent>();
        float screenY = rc.computed_rel[Axis::Y] - scrollOffset;
        // A row is on-screen if any part of its [screenY, screenY+rowH] band
        // intersects the viewport [0, viewportH].
        if (screenY + kRowH > 0.f && screenY < kViewportH) ++onScreenRows;
    }
    // A 400px viewport / 24px rows fits ~16 rows; assert a healthy slice draws
    // at this deep offset (would be ZERO past-cutoff rows under the old bug,
    // since they'd all be wrapped off-screen to the right).
    CHECK(onScreenRows >= 12);
}

// ---------------------------------------------------------------------------
// 20) Find operators: an operator only ever REMOVES rows from the search.
//     Asserted against the mock's r1 thread, whose shape is the point — a user
//     ask, then one assistant turn that runs tools and answers twice. The UI
//     side of this (the tally equals the bands painted, even filtered) is
//     tests/ui/find_operators.e2e; here is the meaning of each operator.
// ---------------------------------------------------------------------------
static size_t rows_kept(const api::Session& s, const std::string& query) {
    const auto q = hanabi::find_ops::parse(query);
    size_t kept = 0;
    for (size_t i = 0; i < s.messages.size(); ++i)
        if (hanabi::find_ops::row_matches(s, i, q)) ++kept;
    return kept;
}

static void test_find_operators() {
    std::printf("test_find_operators\n");
    api::MockClient m;
    auto r = m.get_session("r1");
    CHECK(r.ok);
    if (!r.ok) return;
    const api::Session& s = r.value;

    // The text part is what gets searched for; the operator never reaches it.
    CHECK(hanabi::find_ops::parse("backoff has:tool").text == "backoff");
    CHECK(hanabi::find_ops::parse("has:tool backoff").text == "backoff");
    // A query with no operator is passed through untouched, spaces and all —
    // "6 failures" is a real query in this suite.
    CHECK(hanabi::find_ops::parse("6 failures").text == "6 failures");
    // A colon that is not one of ours stays part of the search text.
    CHECK(hanabi::find_ops::parse("http://x").text == "http://x");
    CHECK(!hanabi::find_ops::parse("http://x").invalid);

    // An operator we do not have is refused rather than searched for.
    CHECK(hanabi::find_ops::parse("is:thinking").invalid);
    CHECK(hanabi::find_ops::parse("has:banana").invalid);
    CHECK(!hanabi::find_ops::parse("has:tool").invalid);

    // Every row qualifies when nothing is asked of it.
    CHECK(rows_kept(s, "backoff") == s.messages.size());

    // r1 is one user ask followed by a single assistant turn (assistant, three
    // tool rows, assistant). has:tool keeps the turn, drops the ask.
    CHECK(rows_kept(s, "backoff has:tool") == s.messages.size() - 1);
    CHECK(rows_kept(s, "backoff is:user") == 1);
    CHECK(rows_kept(s, "backoff is:assistant") == 2);

    // The tool rows in that turn completed, so the turn is state:completed and
    // is not state:failed. An unknown status is not claimed as any state.
    CHECK(rows_kept(s, "backoff state:completed") == s.messages.size() - 1);
    CHECK(rows_kept(s, "backoff state:failed") == 0);
    CHECK(rows_kept(s, "backoff state:running") == 0);

    // Operators AND together, and no combination can add a row back.
    CHECK(rows_kept(s, "backoff is:user has:tool") == 0);
    CHECK(rows_kept(s, "backoff is:assistant has:tool") == 2);
}

int main() {
    std::printf("=== test_e2e ===\n");
    test_list_loads_sorted_and_has_samples();
    test_state_model_and_glyphs();
    test_smart_view_filters();
    test_tab_open_focus_no_duplicate();
    test_tab_preview_keeps_one_slot();
    test_kickoff_opens_new_tab_without_summary();
    test_tab_close_fallback();
    test_tab_switch_between_open_tabs();
    test_tab_reorder_drop_index();
    test_tab_reorder_moves_and_preserves_active();
    test_tab_reorder_edge_cases();
    test_tab_overflow_width();
    test_tab_scroll_clamp_and_max();
    test_tab_scroll_to_show_active();
    test_tab_close_others();
    test_navi_url_shape();
    test_backend_agnostic_defaults();
    test_transcript_cache();
    test_sidebar_scroll_list_single_column();
    test_sidebar_row_manual_order();
    test_find_operators();

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK (%d skipped/pending)\n", g_skipped);
        return 0;
    }
    std::printf("%d failure(s), %d skipped\n", g_failures, g_skipped);
    return 1;
}
