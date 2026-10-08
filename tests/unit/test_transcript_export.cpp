#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "../../src/api/transcript_export.h"

namespace te = hanabi::api::transcript_export;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static api::Message msg(std::string id, api::Role role, std::string text, int64_t at = 0) {
    api::Message m;
    m.id = std::move(id);
    m.role = role;
    m.text = std::move(text);
    m.created_at = at;
    return m;
}

int main() {
    std::printf("=== test_transcript_export ===\n");
    CHECK(te::seq_of("42") == 42u && te::seq_of("007") == 7u);
    CHECK(!te::seq_of("tk1") && !te::seq_of("") && !te::seq_of("12a"));
    CHECK(te::plain_text("say **hi** and `code` __now__") == "say hi and code now");
    CHECK(te::plain_text("**open\nacross lines**") == "**open\nacross lines**");

    api::Session s;
    s.summary.id = "sess";
    s.summary.title = "A title";
    s.messages = {msg("10", api::Role::User, "hello **there**", 1700000000),
                  msg("11", api::Role::Assistant, "hi", 1700000001),
                  msg("12", api::Role::Tool, "ls", 0)};
    s.messages[2].kind = api::EventKind::ToolCall;
    te::Document d;
    CHECK(te::build(s, d) == te::Outcome::Written);
    CHECK(d.records.size() == 2);
    CHECK(d.header.complete && d.header.seq_axis == "valid" && d.header.contents == "messages");
    CHECK(d.records[0].seq == 10u && d.records[0].role == "user" && d.records[0].text == "hello there" &&
          d.records[0].markdown == "hello **there**" && d.records[0].created_at_unix_ms == 1700000000000LL);
    CHECK(d.records[1].role == "assistant" && !d.records[1].created_at_unix_ms.has_value() == false);

    auto j = nlohmann::json::parse(te::to_json(d));
    CHECK(j["metadata"]["schema"] == "puffin.transcript.v1" && j["metadata"]["source"] == "local_cache");
    CHECK(j["records"].size() == 2 && j["records"][0]["seq"] == 10);
    CHECK(!j.contains("schema"));
    const std::string jl = te::to_jsonl(d);
    CHECK(std::count(jl.begin(), jl.end(), '\n') == 3);
    auto first = nlohmann::json::parse(jl.substr(0, jl.find('\n')));
    CHECK(first["kind"] == "export_metadata" && first["metadata"]["schema"] == "puffin.transcript.v1" &&
          first["metadata"]["complete"] == true);
    CHECK(te::markdown_provenance(d.header).empty());

    api::Session windowed = s;
    windowed.has_more_older = true;
    CHECK(te::build(windowed, d) == te::Outcome::Written && !d.header.complete);
    CHECK(te::markdown_provenance(d.header).find("local cache window") != std::string::npos);

    api::Session unordered = s;
    unordered.messages[1].id = "9";
    CHECK(te::build(unordered, d) == te::Outcome::RefusedSeqOrder);

    api::Session dup = s;
    dup.messages[1].id = "10";
    CHECK(te::build(dup, d) == te::Outcome::RefusedSeqOrder);

    api::Session mixed = s;
    mixed.messages[1].id = "";
    mixed.messages[1].local_id = "pending-1";
    CHECK(te::build(mixed, d) == te::Outcome::Written && d.header.seq_axis == "partial");
    CHECK(d.records[0].seq == 10u && !d.records[1].seq.has_value());
    auto mj = nlohmann::json::parse(te::to_json(d));
    CHECK(mj["records"][0].contains("seq") && !mj["records"][1].contains("seq"));

    api::Session noSeq = s;
    noSeq.messages[0].id = "tk1";
    noSeq.messages[1].id = "tk2";
    CHECK(te::build(noSeq, d) == te::Outcome::Written && d.header.seq_axis == "absent");
    auto nj = nlohmann::json::parse(te::to_json(d));
    CHECK(!nj["records"][0].contains("seq"));

    api::Session thinkingOnly = s;
    thinkingOnly.messages = {msg("1", api::Role::Assistant, "hmm")};
    thinkingOnly.messages[0].subtitle = "thinking";
    CHECK(te::build(thinkingOnly, d) == te::Outcome::Empty);

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
