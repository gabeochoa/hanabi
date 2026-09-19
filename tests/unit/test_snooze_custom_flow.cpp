#include <cstdio>
#include <optional>
#include <string>

#include "../../src/ecs/inbox_sync_store.h"
#include "../../src/native_snooze_prompt.h"
#include "../../src/ui/snooze_custom_flow.h"
#include "../../src/ui/snooze_menu.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sc = hanabi::snooze_custom;
namespace np = hanabi::native_snooze_prompt;
namespace sm = hanabi::snooze_menu;
using hanabi::inbox_sync::Store;

static np::Result result(std::uint64_t gen, np::Result::Kind kind, std::int64_t until = 0) {
    np::Result r;
    r.generation = gen;
    r.kind = kind;
    r.until_unix_sec = until;
    r.submitted_at_unix_sec = 1781524800;
    return r;
}

static sc::Gates open_gates(std::uint64_t clientGen = 1) {
    sc::Gates g;
    g.client_generation = clientGen;
    g.session_known = true;
    g.supports_inbox_state = true;
    g.synced = true;
    g.write_pending = false;
    g.now_unix_sec = 1781524800;
    return g;
}

static api::Result<api::InboxStateRead> read_ok(const std::string& body) {
    api::InboxStateRead r;
    r.http_status = 200;
    r.body = body;
    return api::Result<api::InboxStateRead>::success(std::move(r));
}

int main() {
    std::printf("=== test_snooze_custom_flow ===\n");
    const std::int64_t now = 1781524800;
    const std::int64_t later = now + 3600;

    {
        const auto with = sm::item(std::nullopt, now, "row_menu_snooze", false, true);
        CHECK(with.children.size() >= 3);
        const auto& sep = with.children[with.children.size() - 2];
        const auto& custom = with.children.back();
        CHECK(sep.separator && sep.disabled && sep.debug_name == "row_menu_snooze_divider");
        CHECK(!custom.separator && custom.action_id == sm::kCustomId && custom.label == sc::kLeafLabel);
        CHECK(custom.debug_name == "row_menu_snooze_custom");
        CHECK(!sm::parse(custom.action_id));
        const auto without = sm::item(std::nullopt, now, "row_menu_snooze", false, false);
        CHECK(without.children.size() + 2 == with.children.size());
        for (const auto& l : without.children) CHECK(!l.separator && l.action_id != sm::kCustomId);
        const auto snoozed = sm::item(later, now, "row_menu_snooze", false, true);
        CHECK(snoozed.children.empty() && snoozed.action_id == "unsnooze");
        const auto disabled = sm::item(std::nullopt, now, "tab_menu_snooze", true, true);
        CHECK(disabled.children.back().disabled && disabled.children.back().action_id == sm::kCustomId);
    }

    np::Lifecycle life;
    {
        np::Request req;
        req.scope = sc::scope_for("t6");
        const std::uint64_t gen = life.request(req);
        CHECK(gen != 0);
        np::Request again;
        again.scope = sc::scope_for("t2");
        CHECK(life.request(again) == 0);
        const auto take = life.take_for_dispatch();
        CHECK(take && take->generation == gen && take->scope == "session:t6");
        CHECK(!life.take_for_dispatch());
        CHECK(life.begin_show(gen));
        life.deliver(result(gen, np::Result::Kind::Set, later));
        np::Result got;
        CHECK(!life.take_result(gen + 1, &got));
        CHECK(life.take_result(gen, &got) && got.until_unix_sec == later);
        CHECK(!life.take_result(gen, &got));
    }

    {
        Store store(1);
        store.read_landed(1, read_ok(R"({"read":[],"snoozes":{}})"));
        sc::Pending p{7, 1, "t6"};
        const auto s = sc::settle(p, result(7, np::Result::Kind::Set, later), open_gates());
        CHECK(s.verdict == sc::Verdict::Set && s.session_id == "t6" && s.until_unix_sec == later);
        const auto intent = store.begin_write(s.session_id, s.until_unix_sec, now);
        CHECK(intent && intent->session_id == "t6");
        CHECK(store.get("t6") && store.get("t6")->snoozed_until == later);
        CHECK(!store.get("t2"));
    }
    {
        sc::Pending p{7, 1, "t6"};
        auto g = open_gates();
        const auto s = sc::settle(p, result(7, np::Result::Kind::Set, later), g);
        CHECK(s.session_id == "t6");
    }
    {
        sc::Pending p{7, 1, "t6"};
        CHECK(sc::settle(p, result(6, np::Result::Kind::Set, later), open_gates()).verdict == sc::Verdict::Ignored);
        CHECK(sc::settle(p, result(8, np::Result::Kind::Set, later), open_gates()).verdict == sc::Verdict::Ignored);
    }
    {
        sc::Pending p{7, 1, "t6"};
        const auto s = sc::settle(p, result(7, np::Result::Kind::Set, later), open_gates(2));
        CHECK(s.verdict == sc::Verdict::Nothing && s.reason.empty());
    }
    {
        sc::Pending p{7, 1, "t6"};
        for (auto k : {np::Result::Kind::Cancelled, np::Result::Kind::Quiet, np::Result::Kind::Refused}) {
            const auto s = sc::settle(p, result(7, k, later), open_gates());
            CHECK(s.verdict == sc::Verdict::Nothing && s.reason.empty());
        }
    }
    {
        sc::Pending p{7, 1, "t6"};
        auto g = open_gates();
        g.write_pending = true;
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, later), g).reason == "Still saving the last snooze for this thread");
        g = open_gates();
        g.synced = false;
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, later), g).reason == "Snooze is unsynced");
        g = open_gates();
        g.supports_inbox_state = false;
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, later), g).verdict == sc::Verdict::Refused);
        g = open_gates();
        g.session_known = false;
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, later), g).reason == "That thread is no longer listed");
        g = open_gates();
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, now - 1), g).verdict == sc::Verdict::Refused);
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, now + hanabi::snooze_presets::kMaxHorizonSeconds + 1), g).verdict == sc::Verdict::Refused);
        CHECK(sc::settle(p, result(7, np::Result::Kind::Set, now + 60), g).verdict == sc::Verdict::Set);
    }
    {
        np::Lifecycle l2;
        np::Request req;
        req.scope = sc::scope_for("t6");
        const std::uint64_t gen = l2.request(req);
        l2.cancel(gen);
        CHECK(!l2.take_for_dispatch());
        np::Result got;
        CHECK(!l2.take_result(gen, &got));
        np::Request next;
        next.scope = sc::scope_for("t2");
        CHECK(l2.request(next) != 0);
    }
    {
        Store store(1);
        const auto ticket = store.begin_read();
        store.read_landed(1, read_ok(R"({"read":[],"snoozes":{}})"));
        const auto intent = store.begin_write("t6", later, now);
        CHECK(intent);
        store.reset(2);
        CHECK(store.entries().empty() && !store.synced());
        CHECK(store.read_landed(ticket, read_ok(R"({"read":[],"snoozes":{"t6":{"snoozedAt":1,"snoozedUntil":5}}})")).outcome ==
              hanabi::inbox_sync::Outcome::Ignored);
        CHECK(store.entries().empty());
        api::InboxStateWrite w;
        w.http_status = 200;
        w.body = R"({"ok":true,"snoozedAt":1,"snoozedUntil":1781528400})";
        CHECK(store.write_landed(*intent, api::Result<api::InboxStateWrite>::success(std::move(w))).outcome ==
              hanabi::inbox_sync::Outcome::Ignored);
        CHECK(store.entries().empty());
    }

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
