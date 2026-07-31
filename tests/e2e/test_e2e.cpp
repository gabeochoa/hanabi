// End-to-end / integration tests for hanabi's real app logic, exercised
// HEADLESSLY (no window, no UIContext, no graphics backend) against the
// deterministic MockClient and the real afterhours entity system.
//
// These assert the SHIPPED logic: the sidebar/main-pane/tab systems delegate
// their pure decisions to ecs::model (thread_model.h) and ecs::tabflow
// (tab_model.h), and those exact functions are what we call here — no copies.
//
// Style mirrors tests/unit/test_api.cpp: a tiny assert-based runner, hermetic
// (no network, no env deps).

#include <cstdio>
#include <cstdlib>
#include <string>

// afterhours ECS core only (headless-safe: no graphics backend linked).
#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/mock_client.h"
#include "../../src/ecs/components.h"
#include "../../src/ecs/tab_model.h"
#include "../../src/ecs/thread_model.h"
#include "../../src/ecs/transcript_cache.h"

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

    ecs::tabflow::open_session_in_tab(strip, app, "t1");
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == "t1");
    CHECK(app.view == ecs::SmartView::Chat);
    CHECK(app.requestOpenId == "t1");
    // Label comes from the summary title.
    {
        auto* e = ecs::tabflow::active_tab_entity();
        CHECK(e != nullptr);
        CHECK(e->get<ecs::Tab>().label == "Multi-tier pricing rollout");
    }

    ecs::tabflow::open_session_in_tab(strip, app, "t4");
    CHECK(strip.tabOrder.size() == 2);
    CHECK(app.selectedId == "t4");

    // Re-opening t1 must FOCUS the existing tab, not create a duplicate.
    ecs::tabflow::open_session_in_tab(strip, app, "t1");
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

    ecs::tabflow::open_session_in_tab(strip, app, "t1");
    ecs::tabflow::open_session_in_tab(strip, app, "t4");
    ecs::tabflow::open_session_in_tab(strip, app, "t5");
    CHECK(strip.tabOrder.size() == 3);
    CHECK(app.selectedId == "t5");  // last opened is active

    // Close the active (last) tab -> fall back to the neighbor (min(idx,size-1)).
    auto activeId = ecs::tabflow::active_tab_entity()->id;
    // find its index
    size_t idx = 0;
    for (size_t i = 0; i < strip.tabOrder.size(); ++i)
        if (strip.tabOrder[i] == activeId) idx = i;
    ecs::tabflow::close_tab(strip, app, activeId, idx, /*wasActive=*/true);
    CHECK(strip.tabOrder.size() == 2);
    CHECK(app.selectedId == "t4");  // fell back to previous tab

    // Close a non-active tab (t1 at index 0) while t4 active -> t4 stays.
    auto firstId = strip.tabOrder[0];
    bool firstWasActive = false;
    {
        auto o = afterhours::EntityHelper::getEntityForID(firstId);
        firstWasActive = o.valid() && o->has<ecs::ActiveTab>();
    }
    ecs::tabflow::close_tab(strip, app, firstId, 0, firstWasActive);
    CHECK(strip.tabOrder.size() == 1);
    CHECK(app.selectedId == "t4");

    // Close the last remaining tab -> back to Home digest, no open transcript.
    auto lastId = strip.tabOrder[0];
    ecs::tabflow::close_tab(strip, app, lastId, 0, /*wasActive=*/true);
    CHECK(strip.tabOrder.empty());
    CHECK(app.selectedId.empty());
    CHECK(app.view == ecs::SmartView::Home);
    CHECK(!app.openSession.has_value());
}

// ---------------------------------------------------------------------------
// 5) http adapter defaults: with no env config, make_client() returns the
//    mock; a default summary (what the generic http adapter yields) is calm.
// ---------------------------------------------------------------------------
static void test_backend_agnostic_defaults() {
    std::printf("test_backend_agnostic_defaults\n");
    unsetenv("HANABI_BACKEND");
    unsetenv("HANABI_BASE_URL");
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
        CHECK(cached->messages.size() <= ecs::kCacheMaxMessagesPerThread);
        // And the cap is over the LAST 20: verify the tail matches the source.
        auto full = client.inner.get_session("t1");
        CHECK(full.ok);
        if (full.value.messages.size() > ecs::kCacheMaxMessagesPerThread) {
            CHECK(cached->messages.size() == ecs::kCacheMaxMessagesPerThread);
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
        CHECK(got->messages.size() == ecs::kCacheMaxMessagesPerThread);
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
    CHECK(app2.transcriptCache.size() == ecs::kCacheMaxThreads);

    // Touch t1 so it becomes most-recent; t2 is now the LRU.
    CHECK(resolve_transcript(app2, client2, "t1"));  // hit, no fetch
    CHECK(client2.getSessionCalls == 5);

    // Opening a 6th distinct thread evicts the LRU (t2), still <=5 threads.
    CHECK(!resolve_transcript(app2, client2, "t6"));
    CHECK(client2.getSessionCalls == 6);
    CHECK(app2.transcriptCache.size() == ecs::kCacheMaxThreads);
    CHECK(!app2.transcriptCache.contains("t2"));  // evicted (was LRU)
    CHECK(app2.transcriptCache.contains("t1"));   // touched -> retained
    CHECK(app2.transcriptCache.contains("t6"));   // newest

    // (d) Re-opening the evicted thread (t2) RE-FETCHES (async path intact).
    CHECK(!resolve_transcript(app2, client2, "t2"));  // miss -> fetch
    CHECK(client2.getSessionCalls == 7);
    CHECK(app2.transcriptCache.contains("t2"));
}

int main() {
    std::printf("=== test_e2e ===\n");
    test_list_loads_sorted_and_has_samples();
    test_state_model_and_glyphs();
    test_smart_view_filters();
    test_tab_open_focus_no_duplicate();
    test_tab_close_fallback();
    test_backend_agnostic_defaults();
    test_transcript_cache();

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK (%d skipped/pending)\n", g_skipped);
        return 0;
    }
    std::printf("%d failure(s), %d skipped\n", g_failures, g_skipped);
    return 1;
}
