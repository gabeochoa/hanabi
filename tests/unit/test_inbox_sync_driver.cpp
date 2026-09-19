#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../src/ecs/inbox_sync_driver.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using hanabi::inbox_sync::Driver;
using hanabi::inbox_sync::Outcome;
using hanabi::inbox_sync::SnoozeRequest;

struct Latch {
    std::mutex mu;
    std::condition_variable cv;
    bool open = false;
    void release() {
        {
            std::lock_guard<std::mutex> lk(mu);
            open = true;
        }
        cv.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait_for(lk, std::chrono::seconds(20), [&] { return open; });
    }
};

struct HeldClient : api::Client {
    std::shared_ptr<Latch> latch;
    std::atomic<int> reads{0};
    std::atomic<int> writes{0};
    std::string read_body;
    std::string echo_body;

    explicit HeldClient(std::shared_ptr<Latch> l) : latch(std::move(l)) {}
    api::Result<std::vector<api::SessionSummary>> list_sessions() override { return {}; }
    api::Result<api::Session> get_session(const std::string&) override { return {}; }
    std::string backend_label() const override { return "held"; }
    bool supports_inbox_state() const override { return true; }
    api::Result<api::InboxStateRead> read_inbox_state() override {
        ++reads;
        latch->wait();
        api::InboxStateRead r;
        r.http_status = 200;
        r.body = read_body;
        return api::Result<api::InboxStateRead>::success(std::move(r));
    }
    api::Result<api::InboxStateWrite> write_snooze(const std::string&, std::optional<int64_t>) override {
        ++writes;
        latch->wait();
        api::InboxStateWrite w;
        w.http_status = 200;
        w.body = echo_body;
        return api::Result<api::InboxStateWrite>::success(std::move(w));
    }
};

static std::vector<std::string> toasts;
static void toast(const std::string& t) { toasts.push_back(t); }

static long long drain_ms(Driver& d, const std::shared_ptr<api::Client>& c) {
    const auto t0 = std::chrono::steady_clock::now();
    d.drain(c, 1781524800, toast);
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    std::printf("=== test_inbox_sync_driver ===\n");
    auto oldLatch = std::make_shared<Latch>();
    auto oldClient = std::make_shared<HeldClient>(oldLatch);
    oldClient->read_body = R"({"read":[],"snoozes":{"t6":{"snoozedAt":1,"snoozedUntil":1781528400}}})";
    oldClient->echo_body = R"({"ok":true,"snoozedAt":1,"snoozedUntil":1781531000})";
    std::shared_ptr<api::Client> old = oldClient;

    Driver d;
    CHECK(d.client_generation == 1 && !d.store.synced());

    {
        auto quick = std::make_shared<Latch>();
        quick->release();
        auto boot = std::make_shared<HeldClient>(quick);
        boot->read_body = R"({"read":[],"snoozes":{}})";
        std::shared_ptr<api::Client> c = boot;
        d.launch_read(c);
        CHECK(d.reads.size() == 1);
        for (int i = 0; i < 200 && !d.store.synced(); ++i) {
            d.drain(c, 1781524800, toast);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(d.store.synced() && d.reads.empty());
    }

    d.launch_read(old);
    CHECK(d.reads.size() == 1 && d.reads[0].ticket.generation == 1);
    CHECK(!d.read_in_flight_for_current_client() == false);
    d.launch_read(old);
    CHECK(d.reads.size() == 1);
    d.request = SnoozeRequest{"t6", 1781531000};
    CHECK(drain_ms(d, old) <= 50);
    CHECK(d.writes.size() == 1 && d.writes[0].intent.generation == 1);
    CHECK(d.store.get("t6") && d.store.get("t6")->snoozed_until == 1781531000);
    for (int i = 0; i < 100 && (oldClient->reads.load() < 1 || oldClient->writes.load() < 1); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(oldClient->reads.load() == 1 && oldClient->writes.load() == 1);

    d.request = SnoozeRequest{"t2", 1781531000};
    d.on_client_replaced();
    CHECK(d.client_generation == 2 && !d.store.synced() && d.store.entries().empty());
    CHECK(!d.request);
    CHECK(d.reads.size() == 1 && d.writes.size() == 1);
    CHECK(drain_ms(d, old) <= 50);
    CHECK(d.reads.size() == 1 && d.writes.size() == 1);

    auto newLatch = std::make_shared<Latch>();
    auto newClient = std::make_shared<HeldClient>(newLatch);
    newClient->read_body = R"({"read":[],"snoozes":{"t9":{"snoozedAt":1,"snoozedUntil":1781540000}}})";
    newClient->echo_body = R"({"ok":true,"snoozedAt":5,"snoozedUntil":1781531000})";
    std::shared_ptr<api::Client> fresh = newClient;
    CHECK(!d.read_in_flight_for_current_client());
    d.launch_read(fresh);
    CHECK(d.reads.size() == 2 && d.reads[1].ticket.generation == 2);
    d.launch_read(fresh);
    CHECK(d.reads.size() == 2);

    CHECK(d.read_in_flight_for_current_client());
    d.launch_read(fresh);
    CHECK(d.reads.size() == 2);

    newLatch->release();
    for (int i = 0; i < 400 && !d.store.synced(); ++i) {
        CHECK(drain_ms(d, fresh) <= 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(d.store.synced() && d.store.get("t9") && !d.store.get("t6"));
    CHECK(d.reads.size() == 1 && d.reads[0].ticket.generation == 1);
    CHECK(!d.read_in_flight_for_current_client());
    toasts.clear();
    d.request = SnoozeRequest{"t6", 1781531000};
    CHECK(drain_ms(d, fresh) <= 50);
    CHECK(toasts.size() == 1 && toasts[0] == "Still saving the last snooze for this thread");
    CHECK(d.writes.size() == 1 && !d.store.get("t6"));
    CHECK(d.busy("t6") && !d.busy("t2"));

    oldLatch->release();
    for (int i = 0; i < 400 && (!d.reads.empty() || !d.writes.empty()); ++i) {
        CHECK(drain_ms(d, fresh) <= 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(d.reads.empty() && d.writes.empty());
    CHECK(d.store.get("t9") && !d.store.get("t6"));
    CHECK(d.store.synced() && d.store.entries().size() == 1);
    CHECK(!d.busy("t6"));
    CHECK(toasts.size() == 1);

    d.request = SnoozeRequest{"t6", 1781531000};
    CHECK(drain_ms(d, fresh) <= 50);
    CHECK(d.writes.size() == 1 && d.writes[0].intent.generation == 2 && d.store.get("t6"));
    for (int i = 0; i < 400 && !d.writes.empty(); ++i) {
        d.drain(fresh, 1781524800, toast);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(d.writes.empty() && d.store.get("t6") && d.store.get("t6")->snoozed_until == 1781531000);

    old.reset();
    oldClient.reset();

    {
        Driver e;
        auto l1 = std::make_shared<Latch>();
        auto c1 = std::make_shared<HeldClient>(l1);
        c1->read_body = R"({"read":[],"snoozes":{"t6":{"snoozedAt":1,"snoozedUntil":1781528400}}})";
        std::shared_ptr<api::Client> g1 = c1;
        e.launch_read(g1);
        e.on_client_replaced();
        auto l2 = std::make_shared<Latch>();
        auto c2 = std::make_shared<HeldClient>(l2);
        c2->read_body = R"({"read":[],"snoozes":{"t9":{"snoozedAt":1,"snoozedUntil":1781540000}}})";
        std::shared_ptr<api::Client> g2 = c2;
        e.launch_read(g2);
        CHECK(e.reads.size() == 2);
        for (int i = 0; i < 100 && c2->reads.load() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(c2->reads.load() == 1);
        l1->release();
        for (int i = 0; i < 400 && e.reads.size() != 1; ++i) {
            CHECK(drain_ms(e, g2) <= 50);
            e.launch_read(g2);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(e.reads.size() == 1 && e.reads[0].ticket.generation == 2);
        CHECK(!e.store.synced() && e.store.entries().empty());
        for (int i = 0; i < 20; ++i) {
            e.launch_read(g2);
            CHECK(drain_ms(e, g2) <= 50);
        }
        CHECK(e.reads.size() == 1 && c2->reads.load() == 1);
        l2->release();
        for (int i = 0; i < 400 && !e.reads.empty(); ++i) {
            e.drain(g2, 1781524800, toast);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(e.reads.empty() && e.store.synced() && e.store.get("t9") && !e.store.get("t6"));
        CHECK(c2->reads.load() == 1);
    }

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
