// Unit tests for the backend-agnostic API layer. Pure logic only — no network.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/api/http_client.h"
#include "../../src/api/mock_client.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static void test_config_defaults() {
    std::printf("test_config_defaults\n");
    // With nothing set, backend defaults to mock and http is not ready.
    unsetenv("HANABI_BACKEND");
    unsetenv("HANABI_BASE_URL");
    unsetenv("HANABI_API_BASE_URL");
    unsetenv("HANABI_TOKEN");
    // Isolate from any real ~/.config/hanabi/config.json on this machine by
    // pointing HANABI_CONFIG at a path that cannot exist — the zero-config
    // default is what's under test, not a developer's local http config.
    setenv("HANABI_CONFIG", "/nonexistent/hanabi/config.json.test", 1);
    api::Config c = api::Config::from_env();
    CHECK(c.backend == "mock");
    CHECK(!c.http_ready());
    CHECK(c.sessions_path == "/sessions");
    CHECK(c.messages_path == "/sessions/{id}/messages");
}

static void test_factory_falls_back_to_mock() {
    std::printf("test_factory_falls_back_to_mock\n");
    // Requesting http without a base url must NOT crash — it falls back.
    api::Config c;
    c.backend = "http";
    c.base_url = "";
    auto client = api::make_client(c);
    CHECK(client != nullptr);
    CHECK(client->backend_label() == "mock");
}

static void test_mock_list_sorted_desc() {
    std::printf("test_mock_list_sorted_desc\n");
    api::MockClient m;
    auto r = m.list_sessions();
    CHECK(r.ok);
    CHECK(r.value.size() >= 2);
    for (size_t i = 1; i < r.value.size(); ++i)
        CHECK(r.value[i - 1].updated_at >= r.value[i].updated_at);
}

static void test_mock_get_session() {
    std::printf("test_mock_get_session\n");
    api::MockClient m;
    auto r = m.get_session("t1");
    CHECK(r.ok);
    CHECK(r.value.summary.id == "t1");
    CHECK(!r.value.messages.empty());

    auto miss = m.get_session("does-not-exist");
    CHECK(!miss.ok);
    CHECK(!miss.error.empty());
}

// The mock supplies a spread of high-signal states so the UI has something
// real to render. Verify the model is populated (and defaults for http stay
// Unknown/None, which is exercised by config defaults above).
static void test_mock_high_signal_model() {
    std::printf("test_mock_high_signal_model\n");
    api::MockClient m;
    auto r = m.list_sessions();
    CHECK(r.ok);

    int attention = 0, ready = 0, running = 0, parked = 0, archived = 0;
    int blocked = 0, review = 0, done = 0, starred = 0, foldered = 0;
    for (const auto& s : r.value) {
        switch (s.state) {
            case api::ThreadState::Attention: ++attention; break;
            case api::ThreadState::Ready: ++ready; break;
            case api::ThreadState::Running: ++running; break;
            case api::ThreadState::Parked: ++parked; break;
            case api::ThreadState::Archived: ++archived; break;
            default: break;
        }
        if (s.tag == api::ThreadTag::Blocked) ++blocked;
        if (s.tag == api::ThreadTag::Review) ++review;
        if (s.tag == api::ThreadTag::Done) ++done;
        if (s.starred) ++starred;
        if (!s.folder.empty()) ++foldered;
    }
    // Every attention state must be present so smart views have content.
    CHECK(attention > 0);
    CHECK(ready > 0);
    CHECK(running > 0);
    CHECK(parked > 0);
    CHECK(archived > 0);
    // Tags + starring + folders exercised.
    CHECK(blocked > 0);
    CHECK(review > 0);
    CHECK(done > 0);
    CHECK(starred > 0);
    CHECK(foldered > 0);
}

// A default-constructed summary (what the generic http adapter yields when it
// only maps id/title/status) must degrade to a calm, unknown state.
static void test_http_defaults_are_calm() {
    std::printf("test_http_defaults_are_calm\n");
    api::SessionSummary s;
    CHECK(s.state == api::ThreadState::Unknown);
    CHECK(s.tag == api::ThreadTag::None);
    CHECK(!s.starred);
    CHECK(s.folder.empty());
}

// The server-reported attentionState (par-msl/navi#4091) must beat the title
// heuristics when present, must NOT fire on the backend's settled/default
// value, and must leave a signal-less backend exactly as it was.
static void test_attention_state_field() {
    std::printf("test_attention_state_field\n");
    api::Config c;  // defaults: attentionState / needs_user / running

    auto derive = [&c](const char* raw, const char* title) {
        api::SessionSummary s;
        s.title = title ? title : "";
        auto e = nlohmann::json::parse(raw);
        s.status = e.value("status", "");
        api::derive_state(s, e, c);
        return s;
    };

    // needs_user is trusted outright — even for a calm-sounding title.
    auto blocked = derive(R"({"attentionState":"needs_user"})", "just a title");
    CHECK(blocked.state == api::ThreadState::Attention);
    CHECK(blocked.tag == api::ThreadTag::Blocked);

    // running is trusted outright — even when the title screams "waiting".
    auto running = derive(R"({"attentionState":"running"})", "waiting on you");
    CHECK(running.state == api::ThreadState::Running);
    CHECK(running.tag == api::ThreadTag::None);

    // "done" is that API's DEFAULT for every idle session, so it must NOT be
    // read as hanabi's Done nudge — a calm row stays calm...
    auto calm = derive(R"({"attentionState":"done"})", "some ordinary thread");
    CHECK(calm.state == api::ThreadState::Unknown);
    CHECK(calm.tag == api::ThreadTag::None);

    // ...and it must not silence a local signal either: the heuristics still
    // get their say on a parked thread.
    auto parked = derive(R"({"attentionState":"done"})", "[P] needs my call");
    CHECK(parked.state == api::ThreadState::Attention);
    CHECK(parked.tag == api::ThreadTag::Blocked);

    // An unrecognised value is treated like an absent one (forward-compatible).
    auto future = derive(R"({"attentionState":"needs_robot"})", "shipped it");
    CHECK(future.state == api::ThreadState::Attention);
    CHECK(future.tag == api::ThreadTag::Done);

    // No field at all: the pre-#4091 heuristics, untouched.
    auto legacy = derive(R"({"isProcessing":true})", "anything");
    CHECK(legacy.state == api::ThreadState::Running);
    auto quiet = derive(R"({})", "ordinary thread");
    CHECK(quiet.state == api::ThreadState::Unknown);

    // Archived still wins its own row when the server says nothing actionable.
    auto arch = derive(R"({"attentionState":"done","status":"archived"})", "x");
    CHECK(arch.state == api::ThreadState::Archived);

    // A renamed field/value pair is config-driven, not compiled in.
    api::Config renamed;
    renamed.field_attention_state = "triage";
    renamed.field_attention_needs_user = "ON_USER";
    api::SessionSummary s;
    s.title = "calm";
    auto e = nlohmann::json::parse(R"({"triage":"on_user"})");
    api::derive_state(s, e, renamed);
    CHECK(s.state == api::ThreadState::Attention);
    CHECK(s.tag == api::ThreadTag::Blocked);
}

int main() {
    std::printf("=== test_api ===\n");
    test_config_defaults();
    test_factory_falls_back_to_mock();
    test_mock_list_sorted_desc();
    test_mock_get_session();
    test_mock_high_signal_model();
    test_http_defaults_are_calm();
    test_attention_state_field();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
