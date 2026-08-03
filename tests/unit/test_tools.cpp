// Unit tests for the LIVE + memory-light data-layer additions. All PURE — no
// graphics, no network, no timers:
//
//   1. split_message_blocks: a fixture assistant message with INTERLEAVED
//      text/tool_call/tool_result blocks splits, IN ORDER, into the right
//      sequence of Role::Assistant + Role::Tool messages, with the tool name
//      (subtitle), command (text), result output/status, and duration (ms)
//      populated from real block fields.
//   2. MockClient windowed fetch: get_session(id, N) returns the NEWEST N
//      messages (ascending within the window) and sets has_more_older when it
//      truncated — the memory-light newest-N contract, tested deterministically
//      against the HANABI_BIG_TRANSCRIPT fixture.
//   3. parse_events_frame: the live SSE event vocabulary — a top-level
//      {type:"connected"} frame is ignored, a nested event.type activity frame
//      fires on_activity, and context_usage telemetry is ignored.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/api/http_client.h"
#include "../../src/api/mock_client.h"

using json = nlohmann::json;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// --- 1. Tool-call block splitting ------------------------------------------
static void test_split_interleaved_blocks() {
    std::printf("test_split_interleaved_blocks\n");
    api::Config cfg;  // defaults match the verified live block shapes.

    // A real assistant message: text, tool_call+result, more text, another
    // tool_call+result (with node in a stringified inputs blob).
    json e = {
        {"id", "msg1"},
        {"role", "assistant"},
        {"created_at", 1000},
        {"blocks", json::array({
            {{"type", "text"}, {"content", "Let me check that."}},
            {{"type", "tool_call"},
             {"toolCall", {{"id", "tc1"},
                           {"name", "bash"},
                           {"inputs", {{"command", "ls -la"}, {"node", "cli:aspen"}}},
                           {"startedAt", 1000000}}}},
            {{"type", "tool_result"},
             {"toolResult", {{"toolCallId", "tc1"},
                             {"output", "total 8\ndrwxr-xr-x"},
                             {"status", "completed"},
                             {"completedAt", 1026000}}}},
            {{"type", "text"}, {"content", "Now the second step."}},
            {{"type", "tool_call"},
             {"toolCall", {{"id", "tc2"},
                           {"name", "ipython"},
                           {"inputs", "{\"command\":\"print(2+2)\"}"},
                           {"startedAt", 2000}}}},
            {{"type", "tool_result"},
             {"toolResult", {{"toolCallId", "tc2"},
                             {"output", "4"},
                             {"status", "completed"},
                             {"completedAt", 5000}}}},
        })},
    };

    std::vector<api::Message> out = api::split_message_blocks(e, cfg);

    // Expected order: Assistant(text) Tool(tc1) Assistant(text) Tool(tc2).
    CHECK(out.size() == 4);
    CHECK(out[0].role == api::Role::Assistant);
    CHECK(out[0].text == "Let me check that.");

    CHECK(out[1].role == api::Role::Tool);
    CHECK(out[1].subtitle == "bash");                 // name -> subtitle
    CHECK(out[1].text == "[cli:aspen] ls -la");        // command (+node) -> text
    CHECK(out[1].tool_node == "cli:aspen");            // node -> dedicated field
    CHECK(out[1].tool_result == "total 8\ndrwxr-xr-x");
    CHECK(out[1].tool_status == "completed");
    CHECK(out[1].tool_duration_ms == 26000);           // 1026000 - 1000000
    CHECK(out[1].id == "tc1");

    CHECK(out[2].role == api::Role::Assistant);
    CHECK(out[2].text == "Now the second step.");

    // Text fragments of ONE parent message must have DISTINCT ids: the
    // renderer's measure cache is keyed by id, so colliding ids make every
    // fragment inherit the first's cached height and corrupt the virtualized
    // layout (a big real message splits into ~10 text fragments — they must
    // not all share the parent id). Regression guard for the "messages vanish
    // on large real transcripts" bug.
    CHECK(out[0].id != out[2].id);
    CHECK(!out[0].id.empty());
    CHECK(!out[2].id.empty());

    CHECK(out[3].role == api::Role::Tool);
    CHECK(out[3].subtitle == "ipython");
    CHECK(out[3].text == "print(2+2)");                // stringified inputs parsed
    CHECK(out[3].tool_result == "4");
    CHECK(out[3].tool_duration_ms == 3000);            // 5000 - 2000
}

// A message with NO blocks (flat text) yields a single message unchanged.
static void test_split_flat_message() {
    std::printf("test_split_flat_message\n");
    api::Config cfg;
    json e = {{"id", "m"}, {"role", "user"}, {"text", "hi there"}};
    auto out = api::split_message_blocks(e, cfg);
    CHECK(out.size() == 1);
    CHECK(out[0].role == api::Role::User);
    CHECK(out[0].text == "hi there");
}

// An assistant message whose ONLY block is a non-text/non-tool block (e.g.
// `error`) must NOT render as an empty grey bubble (M1). If the block carries
// text (content/text), surface it; if it carries nothing renderable, drop the
// message entirely rather than emit a blank turn.
static void test_split_error_block() {
    std::printf("test_split_error_block\n");
    api::Config cfg;
    // (a) error block WITH content -> surfaced as one text message.
    json withText = {{"id", "e1"}, {"role", "assistant"},
                     {"blocks", json::array({
                         {{"type", "error"}, {"content", "MODULE_NOT_FOUND"}}})}};
    auto a = api::split_message_blocks(withText, cfg);
    CHECK(a.size() == 1);
    CHECK(a[0].text == "MODULE_NOT_FOUND");
    // (b) block with NO text and no tool call -> DROPPED (no empty bubble).
    json empty = {{"id", "e2"}, {"role", "assistant"},
                  {"blocks", json::array({ {{"type", "steering"}} })}};
    auto b = api::split_message_blocks(empty, cfg);
    CHECK(b.empty());
}

// A `steering` block is the USER's mid-stream interjection into a RUNNING
// agent's turn. The backend embeds it inside the ASSISTANT message's blocks[],
// but the words are the user's — so it MUST split out as a Role::User message,
// not fold into the assistant text. Regression guard for "my message shows up
// as your message" (Gabe typed 'still no chat input' into a running agent and
// it rendered as the assistant's text).
static void test_split_steering_is_user() {
    std::printf("test_split_steering_is_user\n");
    api::Config cfg;
    // assistant turn: text, then a user steer, then more assistant text.
    json e = {
        {"id", "msgS"},
        {"role", "assistant"},
        {"created_at", 500},
        {"blocks", json::array({
            {{"type", "text"}, {"content", "Working on it."}},
            {{"type", "steering"}, {"content", "still no chat input"}},
            {{"type", "text"}, {"content", "Got it — investigating."}},
        })},
    };
    auto out = api::split_message_blocks(e, cfg);
    // Order preserved: Assistant, User(steer), Assistant.
    CHECK(out.size() == 3);
    CHECK(out[0].role == api::Role::Assistant);
    CHECK(out[0].text == "Working on it.");
    CHECK(out[1].role == api::Role::User);              // the steer is the USER
    CHECK(out[1].text == "still no chat input");
    CHECK(out[2].role == api::Role::Assistant);
    CHECK(out[2].text == "Got it — investigating.");
    // The steer fragment must have a DISTINCT id (measure-cache keying).
    CHECK(out[1].id != out[0].id);
    CHECK(out[1].id != out[2].id);
    CHECK(!out[1].id.empty());

    // A steering block that also carries `text` (not `content`) still works.
    json e2 = {{"id", "msgT"}, {"role", "assistant"},
               {"blocks", json::array({
                   {{"type", "steering"}, {"text", "hurry up"}}})}};
    auto out2 = api::split_message_blocks(e2, cfg);
    CHECK(out2.size() == 1);
    CHECK(out2[0].role == api::Role::User);
    CHECK(out2[0].text == "hurry up");
}

// --- 2. MockClient memory-light newest-N -----------------------------------
static void test_mock_windowed_newest_n() {
    std::printf("test_mock_windowed_newest_n\n");
    // The big fixture is a deterministic long transcript.
    setenv("HANABI_BIG_TRANSCRIPT", "1", 1);
    api::MockClient m;

    // Full fetch: many messages, no truncation flag.
    auto full = m.get_session("rbig");
    CHECK(full.ok);
    const size_t total = full.value.messages.size();
    CHECK(total > 40);
    CHECK(!full.value.has_more_older);

    // Windowed fetch: exactly the newest N, has_more_older set.
    const int N = 10;
    auto win = m.get_session("rbig", N);
    CHECK(win.ok);
    CHECK(win.value.messages.size() == static_cast<size_t>(N));
    CHECK(win.value.has_more_older);
    // Newest N == the LAST N of the full transcript, order preserved.
    for (int i = 0; i < N; ++i) {
        CHECK(win.value.messages[i].id ==
              full.value.messages[total - N + i].id);
    }

    // limit <= 0 => full transcript, no truncation.
    auto zero = m.get_session("rbig", 0);
    CHECK(zero.ok);
    CHECK(zero.value.messages.size() == total);
    CHECK(!zero.value.has_more_older);

    // A window >= total does not flag has_more_older.
    auto big = m.get_session("rbig", static_cast<int>(total) + 100);
    CHECK(big.ok);
    CHECK(big.value.messages.size() == total);
    CHECK(!big.value.has_more_older);

    unsetenv("HANABI_BIG_TRANSCRIPT");
}

// The mock is a network-free no-op for live events (offline demo stays stable).
static void test_mock_events_noop() {
    std::printf("test_mock_events_noop\n");
    api::MockClient m;
    CHECK(!m.supports_events());
    bool fired = false;
    api::EventSink sink;
    sink.on_activity = [&](const std::string&) { fired = true; };
    auto sub = m.subscribe_events("t1", sink);
    CHECK(sub != nullptr);   // never null
    sub->stop();             // idempotent no-op
    CHECK(!fired);           // nothing ever fires from the mock
}

// --- 3. Live SSE event-frame parsing ---------------------------------------
static void test_parse_events_frame() {
    std::printf("test_parse_events_frame\n");
    api::Config cfg;
    std::vector<std::string> activity;
    api::EventSink sink;
    sink.on_activity = [&](const std::string& k) { activity.push_back(k); };

    std::string carry;
    // 1) connected frame -> ignored.
    api::parse_events_frame(
        "data: {\"type\":\"connected\",\"sessionId\":\"s\",\"ts\":1}\n\n", cfg,
        sink, carry);
    CHECK(activity.empty());

    // 2) context_usage telemetry -> ignored.
    api::parse_events_frame(
        "data: {\"sessionId\":\"s\",\"event\":{\"type\":\"context_usage\"},"
        "\"ts\":2}\n\n",
        cfg, sink, carry);
    CHECK(activity.empty());

    // 3) a real message/turn activity -> fires on_activity with event.type.
    api::parse_events_frame(
        "data: {\"sessionId\":\"s\",\"event\":{\"type\":\"session_start\"},"
        "\"ts\":3}\n\n",
        cfg, sink, carry);
    CHECK(activity.size() == 1);
    CHECK(activity[0] == "session_start");

    // 4) a frame SPLIT across two reads (carry) reassembles + fires once.
    api::parse_events_frame(
        "data: {\"sessionId\":\"s\",\"event\":{\"type\":\"me", cfg, sink,
        carry);
    CHECK(activity.size() == 1);  // incomplete: nothing yet
    api::parse_events_frame("ssage\"}}\n\n", cfg, sink, carry);
    CHECK(activity.size() == 2);
    CHECK(activity[1] == "message");
}

int main() {
    std::printf("=== test_tools ===\n");
    test_split_interleaved_blocks();
    test_split_flat_message();
    test_split_error_block();
    test_split_steering_is_user();
    test_mock_windowed_newest_n();
    test_mock_events_noop();
    test_parse_events_frame();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
