// Unit tests for the backend-agnostic API layer. Pure logic only — no network.
#include <cstdio>
#include <cstdlib>
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

static void test_config_defaults() {
    std::printf("test_config_defaults\n");
    // With nothing set, backend defaults to mock and http is not ready.
    unsetenv("HANABI_BACKEND");
    unsetenv("HANABI_BASE_URL");
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
    auto r = m.get_session("s-welcome");
    CHECK(r.ok);
    CHECK(r.value.summary.id == "s-welcome");
    CHECK(!r.value.messages.empty());
    CHECK(r.value.messages.front().role == api::Role::User);

    auto miss = m.get_session("does-not-exist");
    CHECK(!miss.ok);
    CHECK(!miss.error.empty());
}

int main() {
    std::printf("=== test_api ===\n");
    test_config_defaults();
    test_factory_falls_back_to_mock();
    test_mock_list_sorted_desc();
    test_mock_get_session();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
