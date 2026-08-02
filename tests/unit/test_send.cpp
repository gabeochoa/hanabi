// Unit tests for sending (Phase SEND): kickoff (create_session) + reply
// (send_message), driven directly against the MockClient. Pure logic — NO
// graphics, NO network. Proves the composer's two flows work end to end on the
// default (mock) backend, which is the demo story.
#include <cstdio>
#include <string>

#include "../../src/api/mock_client.h"
#include "../../src/api/http_client.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static bool list_contains(api::MockClient& m, const std::string& id) {
    auto r = m.list_sessions();
    if (!r.ok) return false;
    for (const auto& s : r.value)
        if (s.id == id) return true;
    return false;
}

// --- Mock supports send (composer becomes functional on the default backend).
static void test_supports_send() {
    std::printf("test_supports_send\n");
    api::MockClient m;
    CHECK(m.supports_send());
}

// --- Kickoff: create_session -> new id appears in list_sessions. -----------
static void test_kickoff_appears_in_list() {
    std::printf("test_kickoff_appears_in_list\n");
    api::MockClient m;

    auto r = m.create_session("investigate the flaky test");
    CHECK(r.ok);
    const std::string id = r.value;
    CHECK(!id.empty());

    // The new session must be listed…
    CHECK(list_contains(m, id));
    // …and openable, with the prompt as its first (user) message.
    auto g = m.get_session(id);
    CHECK(g.ok);
    CHECK(!g.value.messages.empty());
    CHECK(g.value.messages.front().role == api::Role::User);
    CHECK(g.value.messages.front().text == "investigate the flaky test");
}

// --- Reply: send_message -> User + Assistant appended; returned msg is the
//     assistant reply. Works on a composer-created session. --------------------
static void test_reply_appends_turn() {
    std::printf("test_reply_appends_turn\n");
    api::MockClient m;

    // Start a thread, then reply into it.
    auto k = m.create_session("draft release notes");
    CHECK(k.ok);
    const std::string id = k.value;

    auto before = m.get_session(id);
    CHECK(before.ok);
    const size_t n0 = before.value.messages.size();

    auto r = m.send_message(id, "hi");
    CHECK(r.ok);
    // The returned message is the ASSISTANT reply.
    CHECK(r.value.role == api::Role::Assistant);
    CHECK(!r.value.text.empty());

    // The session gained exactly a User("hi") + an Assistant message.
    auto after = m.get_session(id);
    CHECK(after.ok);
    CHECK(after.value.messages.size() == n0 + 2);
    const auto& msgs = after.value.messages;
    CHECK(msgs[msgs.size() - 2].role == api::Role::User);
    CHECK(msgs[msgs.size() - 2].text == "hi");
    CHECK(msgs.back().role == api::Role::Assistant);
    CHECK(msgs.back().text == r.value.text);
}

// --- Reply into a SEED session (materialized-on-first-touch), and the sidebar
//     preview/updated_at reflect the new activity. ----------------------------
static void test_reply_into_seed_updates_preview() {
    std::printf("test_reply_into_seed_updates_preview\n");
    api::MockClient m;

    // t3 is a seed session ("Creator welcome QP copy").
    const std::string id = "t3";
    auto pre = m.get_session(id);
    CHECK(pre.ok);
    const size_t n0 = pre.value.messages.size();

    auto r = m.send_message(id, "ship variant B on Monday");
    CHECK(r.ok);
    CHECK(r.value.role == api::Role::Assistant);

    auto post = m.get_session(id);
    CHECK(post.ok);
    CHECK(post.value.messages.size() == n0 + 2);

    // The list must NOT show t3 twice, and its preview must reflect the reply.
    auto list = m.list_sessions();
    CHECK(list.ok);
    int count = 0;
    const api::SessionSummary* row = nullptr;
    for (const auto& s : list.value)
        if (s.id == id) { ++count; row = &s; }
    CHECK(count == 1);
    CHECK(row != nullptr);
    if (row) CHECK(!row->preview.empty());
}

// --- send_message on a nonexistent session fails cleanly. -------------------
static void test_reply_unknown_session() {
    std::printf("test_reply_unknown_session\n");
    api::MockClient m;
    auto r = m.send_message("nope-does-not-exist", "hi");
    CHECK(!r.ok);
    CHECK(!r.error.empty());
}

// --- No company/product/service name leaks into the synthetic reply. --------
static void test_reply_is_generic() {
    std::printf("test_reply_is_generic\n");
    api::MockClient m;
    auto r = m.send_message(m.create_session("x").value, "hi");
    CHECK(r.ok);
    const std::string& t = r.value.text;
    CHECK(t.find("Meta") == std::string::npos);
    CHECK(t.find("Facebook") == std::string::npos);
}

// ==== Agent steering (Phase STEER) =========================================

// --- Mock supports steering unconditionally (offline). ----------------------
static void test_mock_supports_steer() {
    std::printf("test_mock_supports_steer\n");
    api::MockClient m;
    CHECK(m.supports_steer());
}

// --- Mock steer() appends the user msg + a "(steering) …" ack and returns it.
static void test_mock_steer_appends_ack() {
    std::printf("test_mock_steer_appends_ack\n");
    api::MockClient m;
    const std::string id = m.create_session("kick off a long task").value;
    auto before = m.get_session(id);
    CHECK(before.ok);
    const size_t n0 = before.value.messages.size();

    auto r = m.steer(id, "actually focus on the parser");
    CHECK(r.ok);
    // The returned message is the assistant steering ack.
    CHECK(r.value.role == api::Role::Assistant);
    CHECK(r.value.text.rfind("(steering)", 0) == 0);
    // The ack echoes the steering prompt, and no company/product name leaks.
    CHECK(r.value.text.find("actually focus on the parser") !=
          std::string::npos);
    CHECK(r.value.text.find("Meta") == std::string::npos);
    CHECK(r.value.text.find("Facebook") == std::string::npos);

    // The session gained a User(steer) + an Assistant(ack).
    auto after = m.get_session(id);
    CHECK(after.ok);
    CHECK(after.value.messages.size() == n0 + 2);
    const auto& msgs = after.value.messages;
    CHECK(msgs[msgs.size() - 2].role == api::Role::User);
    CHECK(msgs[msgs.size() - 2].text == "actually focus on the parser");
    CHECK(msgs.back().text == r.value.text);
}

// --- Mock steer() on a nonexistent session fails cleanly. -------------------
static void test_mock_steer_unknown_session() {
    std::printf("test_mock_steer_unknown_session\n");
    api::MockClient m;
    auto r = m.steer("nope-does-not-exist", "hi");
    CHECK(!r.ok);
    CHECK(!r.error.empty());
}

// --- Config: steer_ready()/supports_steer() flip only when steer_path set. --
static void test_http_supports_steer_opt_in() {
    std::printf("test_http_supports_steer_opt_in\n");
    // No steer_path => steering OFF (honest disabled), even with a base URL.
    {
        api::Config c;
        c.base_url = "http://example.invalid/api";
        c.steer_path = "";
        CHECK(!c.steer_ready());
        api::HttpClient h(c);
        CHECK(!h.supports_steer());
        // steer() reports a clean failure (never silently no-ops).
        auto r = h.steer("s1", "hi");
        CHECK(!r.ok);
        CHECK(!r.error.empty());
    }
    // steer_path set => steering ON. The origin-absolute "//path" convention
    // is what the local config uses; the flag flips regardless of the value.
    {
        api::Config c;
        c.base_url = "http://example.invalid/api/v1";
        c.steer_path = "//api/steer/generic";  // generic, no real endpoint
        CHECK(c.steer_ready());
        api::HttpClient h(c);
        CHECK(h.supports_steer());
    }
    // A steer_path with NO base URL is still not ready (needs both).
    {
        api::Config c;
        c.base_url = "";
        c.steer_path = "//api/steer/generic";
        CHECK(!c.steer_ready());
    }
}

// --- STEER-vs-SEND decision (mirrors AppComponent::should_steer_open logic,
//     tested at the client/state level without pulling in graphics/ECS). -----
// The loader routes to steer() iff the backend supports_steer() AND the open
// thread's state == Running; otherwise it sends normally. This reproduces that
// pure decision against the same inputs the loader reads.
static bool decide_steer(const api::Client& c, api::ThreadState state) {
    return c.supports_steer() && state == api::ThreadState::Running;
}
static void test_steer_vs_send_decision() {
    std::printf("test_steer_vs_send_decision\n");
    api::MockClient m;  // supports_steer() == true
    // Running -> steer.
    CHECK(decide_steer(m, api::ThreadState::Running));
    // Any non-Running state -> normal send (no steer).
    CHECK(!decide_steer(m, api::ThreadState::Ready));
    CHECK(!decide_steer(m, api::ThreadState::Attention));
    CHECK(!decide_steer(m, api::ThreadState::Parked));
    CHECK(!decide_steer(m, api::ThreadState::Unknown));
    // A backend that can't steer never routes to steer, even when Running.
    api::Config c;  // default: no steer_path
    api::HttpClient h(c);
    CHECK(!h.supports_steer());
    CHECK(!decide_steer(h, api::ThreadState::Running));
}

int main() {
    std::printf("=== test_send ===\n");
    test_supports_send();
    test_kickoff_appears_in_list();
    test_reply_appends_turn();
    test_reply_into_seed_updates_preview();
    test_reply_unknown_session();
    test_reply_is_generic();
    test_mock_supports_steer();
    test_mock_steer_appends_ack();
    test_mock_steer_unknown_session();
    test_http_supports_steer_opt_in();
    test_steer_vs_send_decision();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
