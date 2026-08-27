#include <cstdio>
#include <string>
#include <vector>

#include "../../src/search/find_memo.h"

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);      \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using hanabi::find_memo::Memo;
using hanabi::find_memo::PaintPolicy;
namespace find_ops = hanabi::find_ops;

static api::Message message(std::string id, api::Role role, std::string text,
                            api::EventKind kind = api::EventKind::Text) {
    api::Message m;
    m.id = std::move(id);
    m.role = role;
    m.text = std::move(text);
    m.kind = kind;
    return m;
}

struct Normalizer {
    int* calls;
    std::vector<std::string> operator()(const api::Message& m, bool) const {
        ++*calls;
        return {m.text};
    }
};

static PaintPolicy policy(float width = 600.0f, bool folded = true,
                          std::uint64_t fold_revision = 1) {
    PaintPolicy p;
    p.wrap_width = width;
    p.fold_long_messages = folded;
    p.show_reasoning = true;
    p.fold_revision = fold_revision;
    return p;
}

static api::Session basic_session() {
    api::Session s;
    s.summary.id = "thread";
    s.messages = {
        message("u1", api::Role::User, "Alpha beta alpha"),
        message("a1", api::Role::Assistant, "beta gamma"),
        message("t1", api::Role::Tool, "tool output", api::EventKind::ToolCall),
    };
    s.messages[2].tool_status = "failed";
    return s;
}

static void test_unchanged_frames_are_constant_work() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    const auto q = find_ops::parse("alpha");
    const auto& first = memo.collect(s, 1, q, policy(), Normalizer{&calls});
    CHECK(first.size() == 2);
    CHECK(calls == 2);
    const std::size_t work = memo.stats().message_work;
    const std::size_t rows = memo.stats().rows_visited;
    const auto& second = memo.collect(s, 1, q, policy(), Normalizer{&calls});
    CHECK(second.size() == 2);
    CHECK(calls == 2);
    CHECK(memo.stats().message_work == work);
    CHECK(memo.stats().rows_visited == rows);
    CHECK(memo.stats().result_hits == 1);
}

static void test_query_change_reuses_normalized_text() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    memo.collect(s, 1, find_ops::parse("alpha"), policy(), Normalizer{&calls});
    const auto& matches =
        memo.collect(s, 1, find_ops::parse("gamma"), policy(), Normalizer{&calls});
    CHECK(matches.size() == 1);
    CHECK(matches[0].msg == 1);
    CHECK(calls == 2);
}

static void test_invalid_query_clears_prior_matches() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    CHECK(memo.collect(s, 1, find_ops::parse("alpha"), policy(),
                       Normalizer{&calls}).size() == 2);
    CHECK(memo.collect(s, 1, find_ops::parse("is:nope"), policy(),
                       Normalizer{&calls}).empty());
    CHECK(memo.query().invalid);
}

static void test_content_mutation_rebuilds_only_that_message() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    memo.collect(s, 1, find_ops::parse("delta"), policy(), Normalizer{&calls});
    s.messages[1].text = "delta";
    const auto& matches =
        memo.collect(s, 2, find_ops::parse("delta"), policy(), Normalizer{&calls});
    CHECK(matches.size() == 1);
    CHECK(matches[0].msg == 1);
    CHECK(calls == 3);
}

static void test_append_and_prepend_are_incremental() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    const auto q = find_ops::parse("needle");
    memo.collect(s, 1, q, policy(), Normalizer{&calls});
    s.messages.push_back(message("a2", api::Role::Assistant, "needle"));
    CHECK(memo.collect(s, 2, q, policy(), Normalizer{&calls}).size() == 1);
    CHECK(calls == 3);
    s.messages.insert(s.messages.begin(),
                      message("u0", api::Role::User, "needle first"));
    const auto& matches = memo.collect(s, 3, q, policy(), Normalizer{&calls});
    CHECK(matches.size() == 2);
    CHECK(matches[0].msg == 0);
    CHECK(matches[1].msg == 4);
    CHECK(calls == 4);
}

static void test_operator_ast_and_tool_state_invalidate_matches() {
    Memo memo;
    api::Session s = basic_session();
    int calls = 0;
    const auto& first = memo.collect(s, 1, find_ops::parse("beta is:user"),
                                     policy(), Normalizer{&calls});
    CHECK(first.size() == 1);
    CHECK(first[0].msg == 0);
    const auto& second = memo.collect(s, 1, find_ops::parse("beta has:tool"),
                                      policy(), Normalizer{&calls});
    CHECK(second.size() == 1);
    CHECK(second[0].msg == 1);
    const auto& failed = memo.collect(s, 1, find_ops::parse("beta state:failed"),
                                      policy(), Normalizer{&calls});
    CHECK(failed.size() == 1);
    s.messages[2].tool_status = "completed";
    const auto& changed = memo.collect(s, 2,
                                       find_ops::parse("beta state:failed"),
                                       policy(), Normalizer{&calls});
    CHECK(changed.empty());
    CHECK(calls == 2);
}

static void test_soft_wrap_offsets_and_fold_state_are_keyed() {
    Memo memo;
    api::Session s;
    s.summary.id = "wrap";
    s.messages = {message("a", api::Role::Assistant, "The import completed")};
    int calls = 0;
    const auto q = find_ops::parse("The import");
    const auto& first = memo.collect(s, 1, q, policy(340.0f, true, 1),
                                     Normalizer{&calls});
    CHECK(first.size() == 1);
    CHECK(first[0].line == 0);
    CHECK(first[0].off == 0);
    const auto* hits = memo.line_hits(0, 0);
    CHECK(hits != nullptr);
    CHECK(hits != nullptr && hits->size() == 1 && (*hits)[0] == 0);
    const std::size_t misses = memo.stats().result_misses;
    memo.collect(s, 1, q, policy(341.0f, true, 1), Normalizer{&calls});
    memo.collect(s, 1, q, policy(341.0f, false, 2), Normalizer{&calls});
    CHECK(memo.stats().result_misses == misses + 2);
    CHECK(calls == 1);
}

static void test_event_kind_controls_paintability() {
    Memo memo;
    api::Session s;
    s.summary.id = "events";
    s.messages = {
        message("text", api::Role::Assistant, "needle", api::EventKind::Text),
        message("delivery", api::Role::Assistant, "needle",
                api::EventKind::Delivery),
        message("thinking", api::Role::Assistant, "needle",
                api::EventKind::Thinking),
    };
    int calls = 0;
    const auto& matches = memo.collect(s, 1, find_ops::parse("needle"), policy(),
                                       Normalizer{&calls});
    CHECK(matches.size() == 1);
    CHECK(matches[0].msg == 0);
    CHECK(memo.row_is_paintable(0));
    CHECK(!memo.row_is_paintable(1));
    CHECK(!memo.row_is_paintable(2));
    CHECK(calls == 1);
}

static void test_two_panes_search_the_same_thread_independently() {
    Memo left;
    Memo right;
    api::Session s = basic_session();
    int left_calls = 0;
    int right_calls = 0;
    CHECK(left.collect(s, 1, find_ops::parse("alpha"), policy(),
                       Normalizer{&left_calls}).size() == 2);
    CHECK(right.collect(s, 1, find_ops::parse("gamma"), policy(),
                        Normalizer{&right_calls}).size() == 1);
    CHECK(left.message_has_match(0));
    CHECK(!left.message_has_match(1));
    CHECK(!right.message_has_match(0));
    CHECK(right.message_has_match(1));
}

static void test_cache_is_bounded_without_truncating_matches() {
    Memo memo;
    api::Session s;
    s.summary.id = "huge";
    const std::size_t n = Memo::capacity() + 17;
    s.messages.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        s.messages.push_back(message("m" + std::to_string(i), api::Role::User,
                                     "needle"));
    int calls = 0;
    const auto& matches = memo.collect(s, 1, find_ops::parse("needle"), policy(),
                                       Normalizer{&calls});
    CHECK(matches.size() == n);
    CHECK(memo.entries() <= Memo::capacity());
    const std::size_t work = memo.stats().message_work;
    memo.collect(s, 1, find_ops::parse("needle"), policy(), Normalizer{&calls});
    CHECK(memo.stats().message_work == work);
}

int main() {
    test_unchanged_frames_are_constant_work();
    test_query_change_reuses_normalized_text();
    test_invalid_query_clears_prior_matches();
    test_content_mutation_rebuilds_only_that_message();
    test_append_and_prepend_are_incremental();
    test_operator_ast_and_tool_state_invalidate_matches();
    test_soft_wrap_offsets_and_fold_state_are_keyed();
    test_event_kind_controls_paintability();
    test_two_panes_search_the_same_thread_independently();
    test_cache_is_bounded_without_truncating_matches();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
