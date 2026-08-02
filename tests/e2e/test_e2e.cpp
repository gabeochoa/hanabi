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
    bool has_t1 = false, has_t4 = false, has_t13 = false;
    for (const auto& s : r.value) {
        if (s.id == "t1") has_t1 = true;    // Multi-tier pricing (attention)
        if (s.id == "t4") has_t4 = true;    // Tier upgrade flow (review)
        if (s.id == "t13") has_t13 = true;  // Legacy gifting (archived)
    }
    CHECK(has_t1);
    CHECK(has_t4);
    CHECK(has_t13);
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
        if (s.id == "t3")  // attention/done -> dot
            CHECK(glyph_for(s) == Glyph::Dot);
        if (s.id == "t6")  // running -> none
            CHECK(glyph_for(s) == Glyph::None);
        if (s.id == "t10")  // parked -> none
            CHECK(glyph_for(s) == Glyph::None);
        if (s.id == "t13")  // archived -> none
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
    // The mock seed has: 2 blocked (t1,t2), 2 review/ready (t4,t5),
    // 3 starred (t1,t11,t5).
    CHECK(blocked == 2);
    CHECK(review == 2);
    CHECK(starred == 3);

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
        CHECK(e->get<ecs::Tab>().label == "Multi-tier pricing rollout");
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

int main() {
    std::printf("=== test_e2e ===\n");
    test_list_loads_sorted_and_has_samples();
    test_state_model_and_glyphs();
    test_smart_view_filters();
    test_tab_open_focus_no_duplicate();
    test_tab_close_fallback();
    test_tab_switch_between_open_tabs();
    test_tab_reorder_drop_index();
    test_tab_reorder_moves_and_preserves_active();
    test_tab_reorder_edge_cases();
    test_backend_agnostic_defaults();
    test_transcript_cache();
    test_sidebar_scroll_list_single_column();

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK (%d skipped/pending)\n", g_skipped);
        return 0;
    }
    std::printf("%d failure(s), %d skipped\n", g_failures, g_skipped);
    return 1;
}
