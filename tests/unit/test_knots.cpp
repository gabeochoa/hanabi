// A unit test must never run the real CLI (it would file real issues under
// a real name): compile the runner in its refusing, test-build shape.
#ifndef AFTER_HOURS_ENABLE_E2E_TESTING
#define AFTER_HOURS_ENABLE_E2E_TESTING 1
#endif
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/knots_runner.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace kn = api::knots;

static kn::Context ctx() {
    kn::Context c;
    c.version = "0.1.0";
    c.os = "macOS 15.6";
    c.surface = "composer";
    c.reporter = "someone";
    c.sessionId = "sess-42";
    return c;
}

static std::string arg_after(const std::vector<std::string>& a, const std::string& flag) {
    for (std::size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == flag) return a[i + 1];
    return {};
}

static void test_capture_splits_and_carries_overflow() {
    CHECK(!kn::capture("  \n "));
    const auto c = kn::capture("fix the \"quoted\" thing\nmore detail\nand more");
    CHECK(c && c->title == "fix the \"quoted\" thing" && c->detail == "more detail\nand more");
    const auto big = kn::capture(std::string(600, 'x'));
    CHECK(big && big->title.size() == 500 && big->detail.size() == 100);
}

static void test_a_thread_knot_carries_both_link_spellings() {
    const auto c = ctx();
    const std::string d = kn::description("detail", c);
    CHECK(d.find("Session: sess-42") != std::string::npos);
    CHECK(d.rfind("detail\n\n", 0) == 0);
    CHECK(d.find("Filed from Hanabi for macOS by someone.") != std::string::npos);
    const auto meta = nlohmann::json::parse(kn::metadata(c, kn::Board::App));
    CHECK(meta["sessionId"] == "sess-42" && meta["source"] == "hanabi-in-app");
    const auto args = kn::create_args("ns/x", "t", d, kn::metadata(c, kn::Board::App), kn::labels(c));
    CHECK(args[0] == "knots.issue" && args[1] == "create");
    CHECK(arg_after(args, "--type") == "task" && arg_after(args, "--namespace") == "ns/x");
    CHECK(arg_after(args, "--label").find("triage:untriaged") != std::string::npos);
}

static void test_filing_through_the_mock_runner() {
    kn::runner_override() = kn::MockRunner::run;
    kn::MockRunner::calls().clear();
    unsetenv("HANABI_MOCK_KNOTS");
    auto r = kn::file("the sidebar flickers\nwhen resizing", ctx(), kn::Board::App);
    CHECK(r.ok && r.value.id == "kt-mock1");
    CHECK(r.value.url.find("/gabeochoa/manager/issues/kt-mock1") != std::string::npos);
    CHECK(kn::MockRunner::calls().size() == 1);

    // A membership refusal joins once and retries.
    kn::MockRunner::calls().clear();
    setenv("HANABI_MOCK_KNOTS", "member", 1);
    r = kn::file("x", ctx(), kn::Board::Backend);
    CHECK(r.ok && kn::MockRunner::calls().size() == 3);
    CHECK(kn::MockRunner::calls()[1][1] == "join");
    CHECK(arg_after(kn::MockRunner::calls()[2], "--namespace") == "agentcloud/client");

    // A join that does not help says so; a failure says Knots refused it.
    kn::MockRunner::calls().clear();
    setenv("HANABI_MOCK_KNOTS", "member-stuck", 1);
    r = kn::file("x", ctx(), kn::Board::App);
    CHECK(!r.ok && r.error.find("joining it was refused") != std::string::npos);
    kn::MockRunner::calls().clear();
    setenv("HANABI_MOCK_KNOTS", "fail", 1);
    r = kn::file("x", ctx(), kn::Board::App);
    CHECK(!r.ok && r.error.rfind("Knots refused it:", 0) == 0);
    CHECK(!kn::file("   ", ctx(), kn::Board::App).ok);
    unsetenv("HANABI_MOCK_KNOTS");
    kn::runner_override() = nullptr;
}

static void test_the_real_runner_never_spawns_in_a_test_build() {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    const auto r = kn::run_real({"knots.namespace", "list"});
    CHECK(!r.spawned);
#endif
}

int main() {
    test_capture_splits_and_carries_overflow();
    test_a_thread_knot_carries_both_link_spellings();
    test_filing_through_the_mock_runner();
    test_the_real_runner_never_spawns_in_a_test_build();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
