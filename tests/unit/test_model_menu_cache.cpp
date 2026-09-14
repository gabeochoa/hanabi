// The app's snapshot of the deployment's `models` menu (model_menu_cache.h):
// one ask per open, backoff after a failure, bound to the client it came
// from, and no row for a harness the menu does not carry.

#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "../../src/ecs/model_menu_cache.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using ecs::model::ModelMenuCache;
using Clock = ModelMenuCache::Clock;

// A stand-in client: answers whatever it is told to, counts the asks.
struct FakeClient : api::Client {
    bool serves = true;
    bool fail = false;
    api::ModelMenu answer;
    int asks = 0;
    bool supports_model_menu() const override { return serves; }
    api::Result<api::ModelMenu> model_menu() override {
        ++asks;
        if (fail) return api::Result<api::ModelMenu>::failure("unreachable");
        return api::Result<api::ModelMenu>::success(answer);
    }
    std::string backend_label() const override { return "fake"; }
    api::Result<std::vector<api::SessionSummary>> list_sessions() override {
        return api::Result<std::vector<api::SessionSummary>>::success({});
    }
    api::Result<api::Session> get_session(const std::string&) override {
        return api::Result<api::Session>::failure("fake");
    }
};

static api::ModelMenu menu_with(std::string harness, std::vector<std::string> keys,
                                std::string defaultKey = "") {
    api::ModelMenu m;
    api::ModelMenuHarness row;
    row.harness = std::move(harness);
    for (auto& k : keys) {
        api::ModelMenuEntry e;
        e.key = k;
        e.name = k;
        e.is_default = k == defaultKey;
        e.effort_default = "high";
        e.efforts = {"low", "high"};
        row.models.push_back(std::move(e));
    }
    m.harnesses.push_back(std::move(row));
    return m;
}

// Drive the cache the way the panel does: request, launch synchronously,
// poll. Returns whether an ask went out.
static bool open_panel(ModelMenuCache& c, const std::shared_ptr<FakeClient>& client,
                       Clock::time_point now) {
    if (!c.request_if_stale(client, now)) return false;
    std::promise<api::Result<api::ModelMenu>> p;
    p.set_value(client->model_menu());
    c.launched(client, p.get_future());
    c.poll(now);
    return true;
}

static void test_one_ask_per_open_and_fresh_for_two_hours() {
    std::printf("test_one_ask_per_open_and_fresh_for_two_hours\n");
    ModelMenuCache c;
    auto client = std::make_shared<FakeClient>();
    client->answer = menu_with("native", {"a", "b"}, "a");
    const auto t0 = Clock::now();
    CHECK(open_panel(c, client, t0));
    CHECK(client->asks == 1 && c.loaded && c.error.empty());
    // A second open while fresh asks nothing; neither do frames in between.
    CHECK(!open_panel(c, client, t0 + std::chrono::minutes(1)));
    CHECK(!c.request_if_stale(client, t0 + std::chrono::minutes(30)));
    CHECK(client->asks == 1);
    // Past the refresh age, the next open asks again -- once.
    CHECK(open_panel(c, client, t0 + std::chrono::minutes(121)));
    CHECK(client->asks == 2);
}

static void test_a_failed_ask_arms_a_backoff_not_a_storm() {
    std::printf("test_a_failed_ask_arms_a_backoff_not_a_storm\n");
    ModelMenuCache c;
    auto client = std::make_shared<FakeClient>();
    client->fail = true;
    const auto t0 = Clock::now();
    CHECK(open_panel(c, client, t0));
    CHECK(client->asks == 1 && !c.loaded && c.error == "unreachable");
    // Opens (and frames) inside the backoff ask nothing: the count holds.
    for (int i = 1; i <= 20; ++i)
        CHECK(!c.request_if_stale(client, t0 + std::chrono::seconds(i)));
    CHECK(!open_panel(c, client, t0 + std::chrono::seconds(29)));
    CHECK(client->asks == 1);
    // After the backoff, one more ask.
    CHECK(open_panel(c, client, t0 + std::chrono::seconds(31)));
    CHECK(client->asks == 2);
    // A backend that serves no menu is never asked at all.
    auto none = std::make_shared<FakeClient>();
    none->serves = false;
    ModelMenuCache d;
    CHECK(!d.request_if_stale(none, t0));
    CHECK(none->asks == 0);
}

static void test_a_failed_refresh_keeps_the_last_good_snapshot_of_the_same_client() {
    std::printf("test_a_failed_refresh_keeps_the_last_good_snapshot_of_the_same_client\n");
    ModelMenuCache c;
    auto client = std::make_shared<FakeClient>();
    client->answer = menu_with("native", {"a"}, "a");
    const auto t0 = Clock::now();
    CHECK(open_panel(c, client, t0));
    client->fail = true;
    CHECK(open_panel(c, client, t0 + std::chrono::minutes(121)));
    CHECK(c.loaded);  // the last good snapshot stays
    CHECK(c.row_for("native") != nullptr && c.row_for("native")->models.size() == 1);
    CHECK(c.error == "unreachable");
}

static void test_a_replaced_client_drops_the_menu_and_its_late_answer_is_discarded() {
    std::printf("test_a_replaced_client_drops_the_menu_and_its_late_answer_is_discarded\n");
    ModelMenuCache c;
    auto old = std::make_shared<FakeClient>();
    old->answer = menu_with("native", {"old-only"}, "old-only");
    const auto t0 = Clock::now();
    CHECK(open_panel(c, old, t0));
    CHECK(c.row_for("native") != nullptr && c.row_for("native")->models[0].key == "old-only");

    // An ask for the OLD client is in flight when the backend is replaced.
    std::promise<api::Result<api::ModelMenu>> late;
    CHECK(c.request_if_stale(old, t0 + std::chrono::minutes(121)));
    c.launched(old, late.get_future());

    auto fresh = std::make_shared<FakeClient>();
    fresh->answer = menu_with("native", {"new-only"}, "new-only");
    c.bind(fresh);
    // Replacement clears the old menu: nothing of the old deployment shows.
    CHECK(!c.loaded && c.row_for("native") == nullptr && c.error.empty());
    // The old ask is STILL PENDING (its socket may sit at the reply timeout);
    // it blocks neither the new client's ask nor the UI: the fresh client is
    // asked at once and its rows show, with the old future parked aside.
    CHECK(open_panel(c, fresh, t0 + std::chrono::minutes(122)));
    CHECK(fresh->asks == 1);
    CHECK(c.loaded && c.row_for("native") != nullptr &&
          c.row_for("native")->models[0].key == "new-only");
    CHECK(c.orphans.size() == 1);
    // Now the old client's late answer arrives: discarded whole (poll reports
    // no change), the fresh menu untouched, the orphan reaped.
    late.set_value(api::Result<api::ModelMenu>::success(menu_with("native", {"old-late"})));
    CHECK(!c.poll(t0 + std::chrono::minutes(122)));
    CHECK(c.loaded && c.row_for("native")->models[0].key == "new-only");
    CHECK(c.orphans.empty());
    // And the old client's failure could not have armed a backoff on the new.
    CHECK(!c.in_backoff(t0 + std::chrono::minutes(122)));
}

static void test_the_row_is_this_harness_or_nothing() {
    std::printf("test_the_row_is_this_harness_or_nothing\n");
    ModelMenuCache c;
    auto client = std::make_shared<FakeClient>();
    api::ModelMenu two = menu_with("native", {"n1", "n2"}, "n1");
    two.harnesses.push_back(menu_with("claude_code", {"cc1"}, "cc1").harnesses.front());
    client->answer = two;
    CHECK(open_panel(c, client, Clock::now()));
    const auto* native = c.row_for("native");
    const auto* cc = c.row_for("claude_code");
    CHECK(native != nullptr && native->models.size() == 2 && native->models[0].key == "n1");
    CHECK(cc != nullptr && cc->models.size() == 1 && cc->models[0].key == "cc1");
    // A harness the menu does not carry gets NO row -- not the first one.
    CHECK(c.row_for("codex") == nullptr);
    // A session whose harness the attach did not name gets no row either.
    CHECK(c.row_for("") == nullptr);
    // Before any answer, no row for anything.
    ModelMenuCache empty;
    CHECK(empty.row_for("native") == nullptr);
}

static void test_the_menu_default_is_what_runs_when_the_attach_named_none() {
    std::printf("test_the_menu_default_is_what_runs_when_the_attach_named_none\n");
    // The panel's rule (main_pane_system.h): resolved = the attach's pin or
    // harness default; when the attach named NEITHER, the menu row's
    // `default` marker. Reproduced here over the cache's row.
    ModelMenuCache c;
    auto client = std::make_shared<FakeClient>();
    client->answer = menu_with("native", {"n1", "n2", "n3"}, "n2");
    CHECK(open_panel(c, client, Clock::now()));
    // The attach named a resolution: it wins over the menu's marker.
    CHECK(c.resolved_model("native", "n3") == "n3");
    // The attach named none: the menu row's default marker.
    CHECK(c.resolved_model("native", "") == "n2");
    // A harness the menu does not carry: nothing.
    CHECK(c.resolved_model("codex", "").empty());
    // A menu that marks no default leaves it absent -- nothing is guessed.
    client->answer = menu_with("native", {"n1", "n2"});
    ModelMenuCache d;
    CHECK(open_panel(d, client, Clock::now()));
    CHECK(d.resolved_model("native", "").empty());
    // Before any answer: only what the attach said.
    ModelMenuCache e;
    CHECK(e.resolved_model("native", "").empty());
    CHECK(e.resolved_model("native", "pinned") == "pinned");
}

static void test_rapid_switches_are_bounded_and_refresh_resumes_when_a_slot_drains() {
    std::printf("test_rapid_switches_are_bounded_and_refresh_resumes_when_a_slot_drains\n");
    ModelMenuCache c;
    const auto t0 = Clock::now();
    // Switch backends kMaxOutstanding times, each ask left PENDING.
    std::vector<std::shared_ptr<FakeClient>> clients;
    std::vector<std::promise<api::Result<api::ModelMenu>>> pending;
    for (std::size_t i = 0; i < ModelMenuCache::kMaxOutstanding; ++i) {
        auto cl = std::make_shared<FakeClient>();
        cl->answer = menu_with("native", {"m" + std::to_string(i)}, "m" + std::to_string(i));
        clients.push_back(cl);
        CHECK(c.request_if_stale(cl, t0));
        pending.emplace_back();
        c.launched(cl, pending.back().get_future());
    }
    CHECK(c.outstanding() == ModelMenuCache::kMaxOutstanding);
    CHECK(c.launches == static_cast<int>(ModelMenuCache::kMaxOutstanding));
    // One more switch: at capacity, the new client's ask is DEFERRED -- no
    // launch, no future destroyed, its cache empty and labelled as waiting.
    auto extra = std::make_shared<FakeClient>();
    extra->answer = menu_with("native", {"extra"}, "extra");
    CHECK(!c.request_if_stale(extra, t0));
    CHECK(c.launches == static_cast<int>(ModelMenuCache::kMaxOutstanding));
    CHECK(c.outstanding() == ModelMenuCache::kMaxOutstanding);
    CHECK(!c.loaded && c.row_for("native") == nullptr);
    CHECK(c.deferred_for_capacity());
    // Frames pass; nothing launches while full.
    for (int i = 0; i < 10; ++i) {
        c.poll(t0 + std::chrono::seconds(i));
        CHECK(!c.request_if_stale(extra, t0 + std::chrono::seconds(i)));
    }
    CHECK(c.launches == static_cast<int>(ModelMenuCache::kMaxOutstanding));
    // One old ask drains (its answer is an old client's: discarded, no rows).
    pending[0].set_value(api::Result<api::ModelMenu>::success(menu_with("native", {"m0"})));
    CHECK(!c.poll(t0 + std::chrono::seconds(11)));
    CHECK(!c.loaded && c.row_for("native") == nullptr);
    CHECK(c.outstanding() == ModelMenuCache::kMaxOutstanding - 1);
    // The slot freed: the current client's refresh resumes and lands.
    CHECK(!c.deferred_for_capacity());
    CHECK(open_panel(c, extra, t0 + std::chrono::seconds(11)));
    CHECK(extra->asks == 1);
    CHECK(c.loaded && c.row_for("native") != nullptr &&
          c.row_for("native")->models[0].key == "extra");
    // Drain the rest: every retired future is reaped, none changed the cache.
    for (std::size_t i = 1; i < pending.size(); ++i)
        pending[i].set_value(api::Result<api::ModelMenu>::success(menu_with("native", {"stale"})));
    CHECK(!c.poll(t0 + std::chrono::seconds(12)));
    CHECK(c.outstanding() == 0);
    CHECK(c.row_for("native")->models[0].key == "extra");
}

static void test_a_deferred_refresh_resumes_once_when_a_slot_drains_without_another_click() {
    std::printf("test_a_deferred_refresh_resumes_once_when_a_slot_drains_without_another_click\n");
    ModelMenuCache c;
    const auto t0 = Clock::now();
    std::vector<std::shared_ptr<FakeClient>> clients;
    std::vector<std::promise<api::Result<api::ModelMenu>>> pending;
    for (std::size_t i = 0; i < ModelMenuCache::kMaxOutstanding; ++i) {
        auto cl = std::make_shared<FakeClient>();
        clients.push_back(cl);
        CHECK(c.request_if_stale(cl, t0));
        pending.emplace_back();
        c.launched(cl, pending.back().get_future());
    }
    // The current client opens its panel at capacity: deferred, retained.
    auto cur = std::make_shared<FakeClient>();
    cur->answer = menu_with("native", {"cur"}, "cur");
    CHECK(!c.request_if_stale(cur, t0));
    CHECK(c.deferredWanted);
    // Frames pass with the panel open and nothing drained: no launch, and
    // the retained request is not consumed by asking.
    for (int i = 1; i <= 5; ++i) {
        const auto before = c.outstanding();
        c.poll(t0 + std::chrono::seconds(i));
        CHECK(c.outstanding() == before);
        CHECK(!c.take_deferred(cur, t0 + std::chrono::seconds(i)));
        CHECK(c.deferredWanted);  // still retained
    }
    CHECK(cur->asks == 0);
    // One old request drains: the loader's rule -- outstanding fell, panel
    // open -> take_deferred through the gate -> exactly ONE launch, no click.
    pending[0].set_value(api::Result<api::ModelMenu>::success(menu_with("native", {"old"})));
    const auto before = c.outstanding();
    CHECK(!c.poll(t0 + std::chrono::seconds(6)));
    CHECK(c.outstanding() == before - 1);
    CHECK(c.take_deferred(cur, t0 + std::chrono::seconds(6)));
    std::promise<api::Result<api::ModelMenu>> p;
    p.set_value(cur->model_menu());
    c.launched(cur, p.get_future());
    CHECK(cur->asks == 1);
    CHECK(!c.deferredWanted);
    // A second drain does not launch a second time.
    pending[1].set_value(api::Result<api::ModelMenu>::success(menu_with("native", {"old"})));
    CHECK(c.poll(t0 + std::chrono::seconds(7)));  // the current client's own landed
    CHECK(!c.take_deferred(cur, t0 + std::chrono::seconds(7)));
    CHECK(cur->asks == 1);
    CHECK(c.loaded && c.row_for("native")->models[0].key == "cur");

    // Panel closed while deferred: the retained request is dropped. The
    // clients here die at the end of each iteration ON PURPOSE: a new one
    // may be allocated at the SAME address, and identity by ownership must
    // still read it as a different deployment (identity by pointer did not).
    ModelMenuCache d;
    std::vector<std::promise<api::Result<api::ModelMenu>>> pend2;
    for (std::size_t i = 0; i < ModelMenuCache::kMaxOutstanding; ++i) {
        auto cl = std::make_shared<FakeClient>();
        CHECK(d.request_if_stale(cl, t0));
        pend2.emplace_back();
        d.launched(cl, pend2.back().get_future());
    }
    auto later = std::make_shared<FakeClient>();
    CHECK(!d.request_if_stale(later, t0));
    CHECK(d.deferredWanted);
    d.panel_closed();
    CHECK(!d.deferredWanted);
    pend2[0].set_value(api::Result<api::ModelMenu>::failure("x"));
    d.poll(t0 + std::chrono::seconds(1));
    // The slot freed, but the panel is closed: nothing resumes.
    CHECK(!d.take_deferred(later, t0 + std::chrono::seconds(1)));
    CHECK(later->asks == 0);
    // Replaced while deferred: dropped by bind. Fill the freed slot again so
    // the open is deferred, then switch clients.
    CHECK(d.request_if_stale(later, t0 + std::chrono::seconds(1)));  // room now
    pend2.emplace_back();
    d.launched(later, pend2.back().get_future());
    auto again = std::make_shared<FakeClient>();
    CHECK(!d.request_if_stale(again, t0 + std::chrono::seconds(1)));  // full again
    CHECK(d.deferredWanted);
    auto other = std::make_shared<FakeClient>();
    d.bind(other);
    CHECK(!d.deferredWanted);
}

static void test_a_client_replaced_under_an_open_panel_is_asked_without_a_reopen() {
    std::printf("test_a_client_replaced_under_an_open_panel_is_asked_without_a_reopen\n");
    ModelMenuCache c;
    const auto t0 = Clock::now();
    auto old = std::make_shared<FakeClient>();
    old->answer = menu_with("native", {"old"}, "old");
    CHECK(open_panel(c, old, t0));
    CHECK(c.row_for("native")->models[0].key == "old");
    // The backend is replaced while the panel is OPEN: the loader binds with
    // panelOpen=true; the old catalog is gone at once and the retained
    // request is armed -- no close, no reopen, no click.
    auto fresh = std::make_shared<FakeClient>();
    fresh->answer = menu_with("native", {"fresh"}, "fresh");
    c.bind(fresh, /*panelOpen=*/true);
    CHECK(!c.loaded && c.row_for("native") == nullptr);
    CHECK(c.deferredWanted);
    // The loader's same-frame resume: take_deferred through the gate -> one
    // launch for the new client, which lands.
    CHECK(c.take_deferred(fresh, t0));
    std::promise<api::Result<api::ModelMenu>> p;
    p.set_value(fresh->model_menu());
    c.launched(fresh, p.get_future());
    CHECK(c.poll(t0));
    CHECK(fresh->asks == 1 && c.loaded && c.row_for("native")->models[0].key == "fresh");
    CHECK(!c.deferredWanted);
    // A FAILING new backend under an open panel: one attempt, then backoff --
    // the retained flag is consumed by the gate's pass, not re-armed per frame.
    auto bad = std::make_shared<FakeClient>();
    bad->fail = true;
    c.bind(bad, true);
    CHECK(c.take_deferred(bad, t0));
    std::promise<api::Result<api::ModelMenu>> q;
    q.set_value(bad->model_menu());
    c.launched(bad, q.get_future());
    c.poll(t0);
    CHECK(bad->asks == 1 && c.failedOnce && !c.deferredWanted);
    for (int i = 1; i <= 10; ++i) CHECK(!c.take_deferred(bad, t0 + std::chrono::seconds(i)));
    CHECK(!c.request_if_stale(bad, t0 + std::chrono::seconds(10)));
    CHECK(bad->asks == 1);
    // Replaced with the panel CLOSED: nothing armed, nothing asked.
    auto quiet = std::make_shared<FakeClient>();
    c.bind(quiet, false);
    CHECK(!c.deferredWanted);
    CHECK(!c.take_deferred(quiet, t0 + std::chrono::seconds(11)));
    CHECK(quiet->asks == 0);
}

int main() {
    std::printf("== test_model_menu_cache ==\n");
    std::fflush(stdout);
    test_one_ask_per_open_and_fresh_for_two_hours();
    test_a_failed_ask_arms_a_backoff_not_a_storm();
    test_a_failed_refresh_keeps_the_last_good_snapshot_of_the_same_client();
    test_a_replaced_client_drops_the_menu_and_its_late_answer_is_discarded();
    test_the_row_is_this_harness_or_nothing();
    test_the_menu_default_is_what_runs_when_the_attach_named_none();
    test_rapid_switches_are_bounded_and_refresh_resumes_when_a_slot_drains();
    test_a_deferred_refresh_resumes_once_when_a_slot_drains_without_another_click();
    test_a_client_replaced_under_an_open_panel_is_asked_without_a_reopen();
    if (g_failures) {
        std::printf("test_model_menu_cache: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_model_menu_cache: OK\n");
    return 0;
}
