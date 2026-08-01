// Unit tests for sending (Phase SEND): kickoff (create_session) + reply
// (send_message), driven directly against the MockClient. Pure logic — NO
// graphics, NO network. Proves the composer's two flows work end to end on the
// default (mock) backend, which is the demo story.
#include <cstdio>
#include <string>

#include "../../src/api/mock_client.h"

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

int main() {
    std::printf("=== test_send ===\n");
    test_supports_send();
    test_kickoff_appears_in_list();
    test_reply_appends_turn();
    test_reply_into_seed_updates_preview();
    test_reply_unknown_session();
    test_reply_is_generic();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
