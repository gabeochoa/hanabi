// Unit tests for streaming replies (Phase STREAM). Two surfaces, both PURE —
// NO graphics, NO network, NO timers:
//
//   1. MockClient streaming: a synthetic reply is split into ordered chunks
//      that reassemble EXACTLY into the one-shot reply, delivered both via the
//      sink (send_message_streaming) and via the per-frame drain the loader
//      uses (prepare_stream + a hand-rolled tick loop). Proves the bubble fills
//      across MULTIPLE ticks and the final message is assembled correctly,
//      deterministically, and asserts NO company name leaks into the reply.
//
//   2. The pure SSE parser (parse_sse_chunk): fixture "data: {json}" text —
//      including a frame SPLIT across two network reads (carry) and the generic
//      event kinds from docs/api-parity.md — drives the sink correctly and
//      reports done.
#include <cstdio>
#include <string>
#include <vector>

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

static bool has_company_name(const std::string& t) {
    // Parent-company-identifying tokens that must never appear in content we
    // ship. "buck" is a company build tool that has leaked into mock tool rows
    // twice — keep it here so any recurrence fails a test, not a manual scan.
    for (const char* w : {"Meta", "Facebook", "navibot", "internalfb",
                          "fbsource", "Instagram", "buck test", "Phabricator"})
        if (t.find(w) != std::string::npos) return true;
    return false;
}

// --- The mock advertises streaming support. --------------------------------
static void test_mock_supports_stream() {
    std::printf("test_mock_supports_stream\n");
    api::MockClient m;
    CHECK(m.supports_stream());
}

// --- send_message_streaming: sink receives Thinking, N deltas, Done; the
//     concatenated deltas EXACTLY equal the final message text. --------------
static void test_stream_sink_reassembles() {
    std::printf("test_stream_sink_reassembles\n");
    api::MockClient m;
    const std::string id = m.create_session("draft release notes").value;

    std::string assembled;
    int deltas = 0;
    bool sawThinking = false;
    bool sawDone = false;
    api::Message finalMsg;
    api::StreamSink sink;
    sink.on_delta = [&](const std::string& d) { assembled += d; ++deltas; };
    sink.on_event = [&](const api::StreamEvent& e) {
        if (e.kind == api::StreamEventKind::Thinking) sawThinking = true;
    };
    sink.on_done = [&](const api::Message& mm) { finalMsg = mm; sawDone = true; };
    sink.on_error = [&](const std::string&) { CHECK(false); };

    m.send_message_streaming(id, "hi there friend", sink);

    CHECK(sawThinking);
    CHECK(sawDone);
    CHECK(deltas >= 2);                 // arrived in multiple pieces.
    CHECK(!finalMsg.text.empty());
    CHECK(finalMsg.role == api::Role::Assistant);
    CHECK(assembled == finalMsg.text);  // pieces reassemble exactly.
    CHECK(!has_company_name(finalMsg.text));
    CHECK(!has_company_name(assembled));
}

// --- prepare_stream + a per-frame drain (the LOADER's idiom): the buffer must
//     grow over MULTIPLE ticks and finish equal to the full reply. -----------
static void test_stream_drains_across_ticks() {
    std::printf("test_stream_drains_across_ticks\n");
    api::MockClient m;
    const std::string id = m.create_session("investigate the flaky test").value;

    auto plan = m.prepare_stream(id, "why is it flaky");
    CHECK(plan.ok);
    CHECK(plan.chunks.size() >= 2);     // splittable into multiple ticks.
    CHECK(!has_company_name(plan.final.text));

    // Reassembling the plan's chunks reproduces the final text exactly.
    std::string cat;
    for (const auto& c : plan.chunks) cat += c;
    CHECK(cat == plan.final.text);

    // Simulate the loader draining 2 chunks/frame; record buffer length after
    // each tick to prove it grows incrementally over MORE THAN ONE tick.
    constexpr size_t kPerFrame = 2;
    std::string buffer;
    std::vector<size_t> lengths;
    size_t cursor = 0;
    int ticks = 0;
    while (cursor < plan.chunks.size()) {
        for (size_t k = 0; k < kPerFrame && cursor < plan.chunks.size(); ++k)
            buffer += plan.chunks[cursor++];
        lengths.push_back(buffer.size());
        ++ticks;
    }
    CHECK(ticks >= 2);                          // fills over multiple frames.
    // Strictly increasing buffer length across ticks (visible growth).
    for (size_t i = 1; i < lengths.size(); ++i) CHECK(lengths[i] > lengths[i - 1]);
    CHECK(buffer == plan.final.text);           // fully assembled at the end.

    // The turn was appended to the transcript: User + Assistant.
    auto g = m.get_session(id);
    CHECK(g.ok);
    CHECK(g.value.messages.size() >= 3);        // kickoff user + user + reply.
    CHECK(g.value.messages.back().role == api::Role::Assistant);
    CHECK(g.value.messages.back().text == plan.final.text);
}

// --- Determinism: same prompt -> identical chunks + final, every time. ------
static void test_stream_is_deterministic() {
    std::printf("test_stream_is_deterministic\n");
    api::MockClient a, b;
    auto pa = a.prepare_stream(a.create_session("x").value, "same prompt");
    auto pb = b.prepare_stream(b.create_session("x").value, "same prompt");
    CHECK(pa.ok && pb.ok);
    CHECK(pa.final.text == pb.final.text);
    CHECK(pa.chunks == pb.chunks);
}

// --- prepare_stream on an unknown session fails cleanly (no mutation). ------
static void test_stream_unknown_session() {
    std::printf("test_stream_unknown_session\n");
    api::MockClient m;
    auto plan = m.prepare_stream("nope-nonexistent", "hi");
    CHECK(!plan.ok);
    CHECK(!plan.error.empty());
    CHECK(plan.chunks.empty());
}

// ---------------- Pure SSE parser (http adapter's config seam) --------------

// Basic: three text frames + a done frame, single chunk -> deltas reassemble,
// done reported. Uses the generic default field names / type values.
static void test_sse_parser_basic() {
    std::printf("test_sse_parser_basic\n");
    api::Config cfg;  // generic defaults (type/text/done...).
    std::string assembled_seen;
    bool doneEvent = false;
    api::StreamSink sink;
    sink.on_delta = [&](const std::string& d) { assembled_seen += d; };
    sink.on_event = [&](const api::StreamEvent& e) {
        if (e.kind == api::StreamEventKind::Done) doneEvent = true;
    };

    const std::string sse =
        "data: {\"type\":\"text\",\"text\":\"Hello \"}\n\n"
        "data: {\"type\":\"text\",\"text\":\"world\"}\n\n"
        "data: {\"type\":\"done\"}\n\n";

    std::string carry, assembled;
    bool done = api::parse_sse_chunk(sse, cfg, sink, carry, assembled);
    CHECK(done);
    CHECK(doneEvent);
    CHECK(assembled == "Hello world");
    CHECK(assembled_seen == "Hello world");
    CHECK(carry.empty());
}

// A frame SPLIT across two network reads must be buffered in `carry` and only
// parsed once complete — no partial/garbled delta.
static void test_sse_parser_split_frame() {
    std::printf("test_sse_parser_split_frame\n");
    api::Config cfg;
    std::string seen;
    api::StreamSink sink;
    sink.on_delta = [&](const std::string& d) { seen += d; };

    std::string carry, assembled;
    // First read ends mid-frame (no terminating blank line yet).
    bool d1 = api::parse_sse_chunk("data: {\"type\":\"text\",\"tex", cfg, sink,
                                   carry, assembled);
    CHECK(!d1);
    CHECK(seen.empty());        // nothing emitted from a partial frame.
    // Second read completes the frame + adds a done frame.
    bool d2 = api::parse_sse_chunk("t\":\"joined\"}\n\ndata: {\"type\":\"done\"}\n\n",
                                   cfg, sink, carry, assembled);
    CHECK(d2);
    CHECK(seen == "joined");
    CHECK(assembled == "joined");
}

// Thinking / tool_call / title_update map to on_event with the right kind, and
// custom (config-mapped) field names / type values are honored — proving the
// parser is config-driven, not hardcoded.
static void test_sse_parser_events_and_custom_fields() {
    std::printf("test_sse_parser_events_and_custom_fields\n");
    api::Config cfg;
    cfg.field_event_type = "kind";     // custom mapping.
    cfg.field_event_text = "delta";
    cfg.field_event_title = "heading";
    cfg.event_type_text = "chunk";
    cfg.event_type_thinking = "reasoning";
    cfg.event_type_tool_call = "tool";
    cfg.event_type_title_update = "rename";
    cfg.event_type_done = "end";

    std::vector<api::StreamEventKind> kinds;
    std::string title, toolLabel, text;
    api::StreamSink sink;
    sink.on_delta = [&](const std::string& d) { text += d; };
    sink.on_event = [&](const api::StreamEvent& e) {
        kinds.push_back(e.kind);
        if (e.kind == api::StreamEventKind::TitleUpdate) title = e.payload;
        if (e.kind == api::StreamEventKind::ToolCall) toolLabel = e.payload;
    };

    const std::string sse =
        "data: {\"kind\":\"reasoning\"}\n\n"
        "data: {\"kind\":\"tool\",\"delta\":\"running search\"}\n\n"
        "data: {\"kind\":\"chunk\",\"delta\":\"partial\"}\n\n"
        "data: {\"kind\":\"rename\",\"heading\":\"New Title\"}\n\n"
        "data: {\"kind\":\"end\"}\n\n";

    std::string carry, assembled;
    bool done = api::parse_sse_chunk(sse, cfg, sink, carry, assembled);
    CHECK(done);
    CHECK(text == "partial");
    CHECK(title == "New Title");
    CHECK(toolLabel == "running search");
    bool sawThinking = false, sawTool = false, sawTitle = false, sawDone = false;
    for (auto k : kinds) {
        if (k == api::StreamEventKind::Thinking) sawThinking = true;
        if (k == api::StreamEventKind::ToolCall) sawTool = true;
        if (k == api::StreamEventKind::TitleUpdate) sawTitle = true;
        if (k == api::StreamEventKind::Done) sawDone = true;
    }
    CHECK(sawThinking && sawTool && sawTitle && sawDone);
}

// A malformed / unknown-type frame is skipped without breaking the stream.
static void test_sse_parser_tolerates_garbage() {
    std::printf("test_sse_parser_tolerates_garbage\n");
    api::Config cfg;
    std::string seen;
    api::StreamSink sink;
    sink.on_delta = [&](const std::string& d) { seen += d; };

    const std::string sse =
        "data: not-json\n\n"
        ": a comment line\n\n"
        "data: {\"type\":\"mystery_future_kind\"}\n\n"
        "data: {\"type\":\"text\",\"text\":\"ok\"}\n\n"
        "data: {\"type\":\"done\"}\n\n";
    std::string carry, assembled;
    bool done = api::parse_sse_chunk(sse, cfg, sink, carry, assembled);
    CHECK(done);
    CHECK(seen == "ok");
}

int main() {
    std::printf("=== test_stream ===\n");
    test_mock_supports_stream();
    test_stream_sink_reassembles();
    test_stream_drains_across_ticks();
    test_stream_is_deterministic();
    test_stream_unknown_session();
    test_sse_parser_basic();
    test_sse_parser_split_frame();
    test_sse_parser_events_and_custom_fields();
    test_sse_parser_tolerates_garbage();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
