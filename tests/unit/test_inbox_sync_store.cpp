#include <cstdio>
#include <optional>
#include <string>

#include "../../src/ecs/inbox_sync_store.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using namespace hanabi::inbox_sync;
using api::InboxStateRead;
using api::InboxStateWrite;
using api::Result;

static Result<InboxStateRead> read_ok(const std::string& body) {
    InboxStateRead r;
    r.http_status = 200;
    r.body = body;
    return Result<InboxStateRead>::success(std::move(r));
}
static Result<InboxStateRead> read_login() {
    InboxStateRead r;
    r.http_status = 200;
    r.body = "<!doctype html>";
    r.signed_out = true;
    return Result<InboxStateRead>::success(std::move(r));
}
static Result<InboxStateRead> read_http(int status) {
    InboxStateRead r;
    r.http_status = status;
    return Result<InboxStateRead>{false, std::move(r), "inbox state HTTP " + std::to_string(status), false};
}
static Result<InboxStateWrite> echo_ok(std::optional<int64_t> at, std::optional<int64_t> until) {
    InboxStateWrite w;
    w.http_status = 200;
    w.body = std::string("{\"ok\":true,\"snoozedAt\":") + (at ? std::to_string(*at) : "null") +
             ",\"snoozedUntil\":" + (until ? std::to_string(*until) : "null") + "}";
    return Result<InboxStateWrite>::success(std::move(w));
}
static Result<InboxStateWrite> write_http(int status) {
    InboxStateWrite w;
    w.http_status = status;
    return Result<InboxStateWrite>{false, std::move(w), "snooze write HTTP " + std::to_string(status), false};
}
static Result<InboxStateWrite> write_drop() {
    return Result<InboxStateWrite>::failure("inbox state unreachable: connection reset");
}

static const char* kSnap = R"({"read":[],"archived":[],"starred":[],"lastSeenAt":{},
  "snoozes":{"t6":{"snoozedAt":1000,"snoozedUntil":5000}}})";

int main() {
    std::printf("=== test_inbox_sync_store ===\n");

    {
        Store s(1);
        CHECK(s.phase() == Phase::Unsynced && !s.synced());
        CHECK(!s.begin_write("t2", 9000, 8000));
        CHECK(s.read_landed(1, read_http(500)).outcome == Outcome::Ignored);
        CHECK(!s.synced() && s.entries().empty() && !s.last_error().empty());
        CHECK(s.read_landed(1, read_login()).outcome == Outcome::Ignored);
        CHECK(s.phase() == Phase::SignedOut);
        CHECK(!s.begin_write("t2", 9000, 8000));
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Applied);
        CHECK(s.synced() && s.get("t6") && s.get("t6")->snoozed_until == 5000 &&
              s.get("t6")->snoozed_at == 1000);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        CHECK(s.read_landed(1, read_login()).outcome == Outcome::Ignored);
        CHECK(!s.synced() && s.phase() == Phase::SignedOut && s.get("t6"));
        CHECK(!s.begin_write("t2", 9000, 8000));
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Applied && s.synced());
        CHECK(s.read_landed(1, read_http(502)).outcome == Outcome::Ignored);
        CHECK(!s.synced() && s.phase() == Phase::Unreachable && s.get("t6") &&
              s.last_error() == "could not reach the web app");
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Applied && s.synced());
        InboxStateRead unauth;
        unauth.http_status = 401;
        unauth.signed_out = true;
        CHECK(s.read_landed(1, Result<InboxStateRead>{false, std::move(unauth), "inbox state HTTP 401", false}).outcome == Outcome::Ignored);
        CHECK(s.phase() == Phase::SignedOut && s.get("t6"));
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Applied && s.synced());
        CHECK(s.read_landed(1, read_ok("{}")).outcome == Outcome::Ignored);
        CHECK(s.phase() == Phase::Malformed && s.get("t6") &&
              s.last_error() == "the web app sent something unexpected");
        CHECK(!s.begin_write("t2", 9000, 8000));
        CHECK(s.read_landed(1, read_ok("not json")).outcome == Outcome::Ignored && s.phase() == Phase::Malformed);
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Applied && s.synced());
        CHECK(s.read_landed(1, Result<InboxStateRead>::failure("inbox state unreachable: timeout")).outcome == Outcome::Ignored);
        CHECK(s.phase() == Phase::Unreachable && s.last_error() == "could not reach the web app" && s.get("t6"));
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        const auto old_read = s.begin_read();
        auto in = s.begin_write("t2", 9000, 8000);
        CHECK(s.write_landed(*in, echo_ok(8001, 9000)).outcome == Outcome::Confirmed);
        const char* snap_other = R"({"read":[],"snoozes":{"t9":{"snoozedAt":1,"snoozedUntil":7000}}})";
        CHECK(s.read_landed(old_read, read_ok(snap_other)).outcome == Outcome::Applied);
        CHECK(s.get("t2") && s.get("t2")->snoozed_until == 9000);
        CHECK(s.get("t9") && !s.get("t6"));
        CHECK(s.read_landed(s.begin_read(), read_ok(snap_other)).outcome == Outcome::Applied);
        CHECK(!s.get("t2") && s.get("t9"));
        s.read_landed(s.begin_read(), read_ok(kSnap));
        const auto before_clear = s.begin_read();
        in = s.begin_write("t6", std::nullopt, std::nullopt);
        CHECK(s.write_landed(*in, echo_ok(std::nullopt, std::nullopt)).outcome == Outcome::Confirmed);
        CHECK(s.read_landed(before_clear, read_ok(kSnap)).outcome == Outcome::Applied);
        CHECK(!s.get("t6"));
        CHECK(s.read_landed(s.begin_read(), read_ok(kSnap)).outcome == Outcome::Applied && s.get("t6"));
        const auto stale = s.begin_read();
        in = s.begin_write("t2", 9500, 8100);
        s.write_landed(*in, echo_ok(8101, 9500));
        CHECK(s.read_landed(stale, read_login()).outcome == Outcome::Ignored && s.phase() == Phase::SignedOut);
        CHECK(s.get("t2") && s.get("t2")->snoozed_until == 9500);
        s.reset(2);
        CHECK(s.mutation_epoch() == 0);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto first = s.begin_write("t2", 9000, 8000);
        CHECK(first && s.pending("t2"));
        CHECK(!s.begin_write("t2", 9500, 8100));
        CHECK(s.get("t2")->snoozed_until == 9000);
        CHECK(s.begin_write("t9", 7000, 6000).has_value());
        CHECK(s.write_landed(*first, echo_ok(8001, 9000)).outcome == Outcome::Confirmed);
        CHECK(!s.pending("t2"));
        auto second = s.begin_write("t2", 9500, 8100);
        CHECK(second && second->before && second->before->snoozed_until == 9000);
        CHECK(s.write_landed(*second, write_http(409)).outcome == Outcome::RolledBack);
        CHECK(s.get("t2")->snoozed_until == 9000);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t2", 9000, 8000);
        CHECK(s.read_landed(1, read_login()).outcome == Outcome::Ignored && !s.synced());
        CHECK(s.pending("t2") && s.get("t2"));
        CHECK(s.write_landed(*in, echo_ok(8001, 9000)).outcome == Outcome::Confirmed);
        CHECK(s.get("t2")->snoozed_at == 8001);
    }
    {
        Store s(1);
        CHECK(s.read_landed(2, read_ok(kSnap)).outcome == Outcome::Ignored);
        CHECK(!s.synced());
        s.read_landed(1, read_ok(kSnap));
        s.reset(2);
        CHECK(!s.synced() && s.entries().empty() && s.generation() == 2);
        CHECK(s.read_landed(1, read_ok(kSnap)).outcome == Outcome::Ignored);
        CHECK(s.entries().empty());
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t2", 9000, 8000);
        CHECK(in && in->ticket == 1 && in->generation == 1 && !in->before);
        CHECK(s.get("t2") && s.get("t2")->snoozed_until == 9000 && s.get("t2")->snoozed_at == 8000);
        CHECK(s.pending("t2"));
        CHECK(s.write_landed(*in, echo_ok(8123, 9000)).outcome == Outcome::Confirmed);
        CHECK(!s.pending("t2") && s.get("t2")->snoozed_at == 8123 && s.get("t2")->snoozed_until == 9000);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t2", 9000, 8000);
        auto c = s.write_landed(*in, echo_ok(8123, 9001));
        CHECK(c.outcome == Outcome::RolledBack && !s.get("t2"));
        in = s.begin_write("t2", 9000, 8000);
        InboxStateWrite w;
        w.http_status = 200;
        w.body = "{\"ok\":true}";
        c = s.write_landed(*in, Result<InboxStateWrite>::success(std::move(w)));
        CHECK(c.outcome == Outcome::RolledBack && !s.get("t2") && !c.reason.empty());
        in = s.begin_write("t2", 9000, 8000);
        InboxStateWrite w2;
        w2.http_status = 200;
        w2.body = "{\"ok\":false,\"snoozedAt\":1,\"snoozedUntil\":9000}";
        c = s.write_landed(*in, Result<InboxStateWrite>::success(std::move(w2)));
        CHECK(c.outcome == Outcome::RolledBack && !s.get("t2"));
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t6", 9000, 8000);
        CHECK(in->before && in->before->snoozed_until == 5000);
        CHECK(s.get("t6")->snoozed_until == 9000);
        auto c = s.write_landed(*in, write_http(409));
        CHECK(c.outcome == Outcome::RolledBack && c.reason == "Snooze refused: too many snoozes");
        CHECK(s.get("t6")->snoozed_until == 5000 && s.get("t6")->snoozed_at == 1000);
        in = s.begin_write("t6", 9000, 8000);
        c = s.write_landed(*in, write_http(400));
        CHECK(c.reason == "Snooze refused: outside the allowed window" && s.get("t6")->snoozed_until == 5000);
        in = s.begin_write("t6", 9000, 8000);
        c = s.write_landed(*in, write_http(403));
        CHECK(c.reason == "Snooze refused: not signed in to the web app");
        in = s.begin_write("t6", 9000, 8000);
        c = s.write_landed(*in, write_drop());
        CHECK(c.reason == "Snooze not saved: could not reach the web app" && s.get("t6")->snoozed_until == 5000);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t6", std::nullopt, std::nullopt);
        CHECK(!s.get("t6") && s.pending("t6"));
        CHECK(s.write_landed(*in, echo_ok(std::nullopt, std::nullopt)).outcome == Outcome::Confirmed);
        CHECK(!s.get("t6"));
        s.read_landed(1, read_ok(kSnap));
        in = s.begin_write("t6", std::nullopt, std::nullopt);
        CHECK(s.write_landed(*in, echo_ok(1000, 5000)).outcome == Outcome::RolledBack);
        CHECK(s.get("t6") && s.get("t6")->snoozed_until == 5000);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto first = s.begin_write("t2", 9000, 8000);
        Intent stale = *first;
        stale.ticket = 99;
        CHECK(s.write_landed(stale, write_http(409)).outcome == Outcome::Ignored);
        CHECK(s.get("t2") && s.get("t2")->snoozed_until == 9000 && s.pending("t2"));
        Intent foreign = *first;
        foreign.generation = 7;
        CHECK(s.write_landed(foreign, write_http(409)).outcome == Outcome::Ignored);
        CHECK(s.write_landed(*first, echo_ok(8150, 9000)).outcome == Outcome::Confirmed);
        CHECK(s.write_landed(*first, write_http(409)).outcome == Outcome::Ignored);
        CHECK(s.get("t2")->snoozed_until == 9000 && s.get("t2")->snoozed_at == 8150);
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        auto in = s.begin_write("t2", 9000, 8000);
        const char* snap2 = R"({"read":[],"snoozes":{"t9":{"snoozedAt":1,"snoozedUntil":7000}}})";
        CHECK(s.read_landed(1, read_ok(snap2)).outcome == Outcome::Applied);
        CHECK(s.get("t2") && s.get("t2")->snoozed_until == 9000);
        CHECK(s.get("t9") && !s.get("t6"));
        CHECK(s.write_landed(*in, echo_ok(8001, 9000)).outcome == Outcome::Confirmed);
        CHECK(s.read_landed(1, read_ok(snap2)).outcome == Outcome::Applied);
        CHECK(!s.get("t2") && s.get("t9"));
    }
    {
        Store s(1);
        s.read_landed(1, read_ok(kSnap));
        CHECK(!s.begin_write("", 9000, 8000));
        CHECK(!s.begin_write(std::string(300, 'x'), 9000, 8000));
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
