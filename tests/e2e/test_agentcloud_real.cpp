// A REAL agentcloud session, end to end. Not the mock, not a fixture.
//
// WHY THIS EXISTS. "i cant see your messages to me you need to test and
// validate that works". The defect it is named for — a live reply arriving as
// the model's reasoning and its tool-call JSON concatenated with the answer —
// was invisible to every offline test in this repo and would have stayed
// invisible to any number of new ones: the mock emits what the mock was
// written to emit, and what the mock was written to emit was what the client
// already understood. Only a real session says what the wire actually carries.
//
// It is OPT-IN and self-skipping, so `make test` stays offline and green on a
// machine with no proxy and no credential:
//   * HANABI_AC_* must name a reachable orchestrator (see the makefile's `run`
//     recipe; the values live in .env, which is gitignored on purpose);
//   * HANABI_AC_PROBE_SESSION must name a session to read;
//   * HANABI_AC_PROBE_SEND=1 additionally sends ONE turn to that session.
// Missing any of those prints SKIP and exits 0.
//
//   make test-agentcloud-real
//
// THE READ HALF is safe on any session: attach, page, fold, and assert that
// the transcript contains what a session of that kind emits.
//
// THE SEND HALF needs a session you own and do not mind writing to — it posts
// one prompt and reads the reply. Point it at a scratch session.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "../../src/api/agentcloud_client.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

static const char* kind_word(api::EventKind k) {
    switch (k) {
        case api::EventKind::Text: return "Text";
        case api::EventKind::Thinking: return "Thinking";
        case api::EventKind::ToolCall: return "ToolCall";
        case api::EventKind::SubAgent: return "SubAgent";
        case api::EventKind::Node: return "Node";
        case api::EventKind::Skill: return "Skill";
        case api::EventKind::Delivery: return "Delivery";
        case api::EventKind::Notice: return "Notice";
        case api::EventKind::Status: return "Status";
        case api::EventKind::Unsupported: return "Unsupported";
        case api::EventKind::Plan: return "Plan";
        case api::EventKind::Goal: return "Goal";
    }
    return "?";
}

static std::string env_or_empty(const char* k) {
    const char* v = std::getenv(k);
    return (v != nullptr) ? std::string(v) : std::string();
}

int main() {
    std::printf("test_agentcloud_real (a live session, read + one turn)\n");

    api::agentcloud::AuthConfig cfg = api::agentcloud::auth_config_from_env();
    const std::string session = env_or_empty("HANABI_AC_PROBE_SESSION");
    if (!cfg.configured() || session.empty()) {
        std::printf("  SKIP: needs HANABI_AC_* configured (%d) and "
                    "HANABI_AC_PROBE_SESSION set (%d)\n",
                    (int)cfg.configured(), (int)!session.empty());
        return 0;
    }
    std::printf("  orchestrator=%s session=%.8s\n", cfg.host.c_str(),
                session.c_str());

    api::AgentcloudClient client(cfg);

    // ---- 1. The catalog is reachable and the credential is good -----------
    auto list = client.list_sessions();
    CHECK(list.ok);
    if (!list.ok) {
        std::printf("  list_sessions error: %s\n", list.error.c_str());
        std::printf("  (nothing else can run without a credential)\n");
        return 1;
    }
    std::printf("  list_sessions: %zu threads\n", list.value.size());

    // ---- 2. A real transcript folds into rows the UI can draw -------------
    //
    // 500 frames rather than the app's default 200: a busy session spends
    // most of its frames on tool machinery, so a small window can genuinely
    // contain no assistant text at all, and this must not be a flaky test
    // ABOUT text being missing.
    auto tx = client.get_session(session, 500);
    CHECK(tx.ok);
    if (!tx.ok) {
        std::printf("  get_session error: %s\n", tx.error.c_str());
        return 1;
    }
    int counts[10] = {0};
    for (const auto& m : tx.value.messages) ++counts[static_cast<int>(m.kind)];
    std::printf("  transcript: %zu rows\n", tx.value.messages.size());
    for (int i = 0; i < 10; ++i)
        if (counts[i] > 0)
            std::printf("    %5d %s\n", counts[i],
                        kind_word(static_cast<api::EventKind>(i)));
    CHECK(!tx.value.messages.empty());

    // Every row the UI draws needs text or a label; a row with neither is a
    // blank line the reader cannot account for.
    for (const auto& m : tx.value.messages)
        CHECK(!m.text.empty() || !m.subtitle.empty());

    // An event row is not authored by anybody and must never render as speech.
    for (const auto& m : tx.value.messages)
        if (m.kind == api::EventKind::Node || m.kind == api::EventKind::Skill ||
            m.kind == api::EventKind::Delivery ||
            m.kind == api::EventKind::SubAgent)
            CHECK(m.role == api::Role::System);

    // ---- 3. A live turn is the agent's answer and nothing else ------------
    if (env_or_empty("HANABI_AC_PROBE_SEND") != "1") {
        std::printf("  (send half skipped: set HANABI_AC_PROBE_SEND=1)\n");
        std::printf("%s\n", g_failures ? "  FAILED" : "OK");
        return g_failures ? 1 : 0;
    }

    // A token this build minted, so a reply that merely echoes the transcript
    // cannot pass. The prompt asks for a TOOL CALL as well as the word: a turn
    // with no tool call cannot exhibit the defect being guarded against, since
    // the tool-call JSON is what used to end up in the bubble.
    const std::string token =
        "PONG-" + std::to_string(static_cast<long long>(std::time(nullptr)));
    const std::string prompt =
        "Call the step tool once with the text \"probing\", then reply with "
        "exactly: " + token;

    api::StreamSink sink;
    std::string streamed;
    int thinking = 0, tools = 0;
    api::Message final_msg;
    std::string error;
    sink.on_delta = [&](const std::string& d) { streamed += d; };
    sink.on_event = [&](const api::StreamEvent& e) {
        if (e.kind == api::StreamEventKind::Thinking) ++thinking;
        if (e.kind == api::StreamEventKind::ToolCall) ++tools;
    };
    sink.on_done = [&](const api::Message& m) { final_msg = m; };
    sink.on_error = [&](const std::string& e) { error = e; };

    client.send_message_streaming(session, prompt, sink);

    if (!error.empty()) {
        std::printf("  stream error: %s\n", error.c_str());
        ++g_failures;
        std::printf("  FAILED\n");
        return 1;
    }
    std::printf("  reply (%zu bytes): [%s]\n", final_msg.text.size(),
                final_msg.text.c_str());
    std::printf("  events: %d thinking, %d tool calls\n", thinking, tools);

    // THE ASSERTION THIS FILE EXISTS FOR. Not "contains" — EQUALS. The defect
    // was extra content, so a containment check would have passed throughout.
    CHECK(final_msg.text == token);
    CHECK(streamed == final_msg.text);
    // The turn really did call a tool, so the guard above was exercised.
    CHECK(tools >= 1);

    std::printf("%s\n", g_failures ? "  FAILED" : "OK");
    return g_failures ? 1 : 0;
}
