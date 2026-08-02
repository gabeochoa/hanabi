// Read-only REAL-backend smoke test.
//
// Unlike the other suites (which are pure/offline against the mock), this one
// exercises the ACTUAL configured http backend end-to-end — list_sessions +
// get_session — to prove hanabi works against real data before a push. It is
// strictly READ-ONLY: it never creates, sends, streams, or mutates anything.
//
// It is OPT-IN and self-skipping so it never breaks the default offline
// `make test`:
//   * requires a TLS build (HANABI_ENABLE_TLS) — a plain build can't do https;
//   * requires a real http backend to be configured (config file or env);
//   * if either is missing it prints SKIP and exits 0.
// Run it explicitly with:  make test-real   (builds TLS + runs this only)
//
// Because it hits the network it is NOT part of the default `make test` gate;
// it's the pre-push "does it work with real data?" check.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/api/client.h"
#include "../../src/api/http_client.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

int main() {
    std::printf("test_real (read-only real-backend smoke)\n");

    api::Config cfg = api::Config::from_env();

    // Skip cleanly when there's no real backend to talk to. This keeps the
    // suite green on a dev box with only the mock, and makes the test safe to
    // wire into CI that has no credentials.
    if (cfg.backend != "http" || !cfg.http_ready()) {
        std::printf("  SKIP: no http backend configured "
                    "(backend=%s, base_url set=%d)\n",
                    cfg.backend.c_str(), (int)!cfg.base_url.empty());
        return 0;
    }
#ifndef HANABI_ENABLE_TLS
    // A non-TLS build can't speak https; the adapter would return a clean error
    // rather than real data. Skip so a plain build isn't a false failure.
    if (cfg.base_url.rfind("https://", 0) == 0) {
        std::printf("  SKIP: https backend but this is a non-TLS build "
                    "(build with HANABI_TLS=1)\n");
        return 0;
    }
#endif

    std::printf("  backend=http base_url=%.40s%s\n", cfg.base_url.c_str(),
                cfg.base_url.size() > 40 ? "..." : "");

    api::HttpClient client(cfg);

    // 1) list_sessions must succeed and return at least one real session with
    //    the core fields populated (id + a non-empty title OR preview).
    auto list = client.list_sessions();
    CHECK(list.ok);
    if (!list.ok) {
        std::printf("  list_sessions error: %s\n", list.error.c_str());
        std::printf("  (cannot continue without a session list)\n");
        std::printf("%s\n", g_failures ? "  test_real FAILED" : "OK");
        return g_failures ? 1 : 0;
    }
    std::printf("  list_sessions: %zu sessions\n", list.value.size());
    CHECK(!list.value.empty());

    if (!list.value.empty()) {
        const auto& s0 = list.value.front();
        CHECK(!s0.id.empty());
        // A real row should carry at least an id + some human text.
        CHECK(!s0.title.empty() || !s0.preview.empty());

        // 2) get_session on the first real id must succeed and parse into a
        //    transcript with at least one message that has a non-empty role/text
        //    (proves the block[]-aware transcript parsing works on real data).
        auto tx = client.get_session(s0.id);
        CHECK(tx.ok);
        if (tx.ok) {
            std::printf("  get_session(%.16s): %zu messages\n", s0.id.c_str(),
                        tx.value.messages.size());
            CHECK(tx.value.summary.id == s0.id);
            // Most real threads have messages; assert the transcript parsed
            // (empty is allowed for a brand-new thread, but if present, the
            // first message must have text).
            if (!tx.value.messages.empty()) {
                bool anyText = false;
                for (const auto& m : tx.value.messages)
                    if (!m.text.empty()) { anyText = true; break; }
                CHECK(anyText);
            }
        } else {
            std::printf("  get_session error: %s\n", tx.error.c_str());
        }
    }

    // 3) FEATURE #4: read user settings from the real backend (GET /whoami).
    //    Read-only. Proves the settings-read wiring works against real data; a
    //    backend without the endpoint would return HTML/404 and fail parsing
    //    (surfaced here, not shipped as a silent no-op).
    if (client.supports_settings()) {
        auto st = client.get_settings();
        CHECK(st.ok);
        if (st.ok) {
            std::printf("  get_settings: user_id=%s sessions=%lld "
                        "schedules=%lld skills=%lld\n",
                        st.value.user_id.c_str(),
                        (long long)st.value.session_count,
                        (long long)st.value.schedule_count,
                        (long long)st.value.skill_count);
            // A real /whoami carries at least a user identity.
            CHECK(!st.value.user_id.empty());
        } else {
            std::printf("  get_settings error: %s\n", st.error.c_str());
        }
    } else {
        std::printf("  get_settings: SKIP (settings_path not configured)\n");
    }

    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("  test_real: %d failure(s)\n", g_failures);
    return 1;
}
