#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/reply_quote.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

namespace rq = hanabi::reply_quote;

static std::string seed_of(const std::string& text,
                           const std::vector<std::string>& handles = {}) {
    const auto s = rq::seed(text, handles);
    return s ? *s : std::string();
}

static void a_quote_is_the_marker_the_opener_and_a_blank_line() {
    CHECK(seed_of("rebase the stack onto master") ==
          "> rebase the stack onto master\n\n");
}

static void the_opener_is_the_first_line_only() {
    CHECK(seed_of("first line\nsecond line") == "> first line\n\n");
}

static void a_fence_is_skipped_whole_and_the_prose_under_it_is_quoted() {
    CHECK(seed_of("```swift\nlet x = 1\n```\nwhat does this do?") ==
          "> what does this do?\n\n");
}

static void an_unclosed_fence_falls_back_to_the_code_inside_it() {
    CHECK(seed_of("```\nmake -j10 output") == "> make -j10 output\n\n");
}

static void a_message_that_is_only_a_fence_marker_still_offers_a_quote() {
    CHECK(rq::is_offered("```", false));
    CHECK(seed_of("```") == "> ```\n\n");
}

static void block_markers_come_off_and_inline_markers_stay_on() {
    CHECK(seed_of("## The heading") == "> The heading\n\n");
    CHECK(seed_of("> already quoted") == "> already quoted\n\n");
    CHECK(seed_of("- a bullet") == "> a bullet\n\n");
    CHECK(seed_of("3. third") == "> third\n\n");
    CHECK(seed_of("| cell | other |") == "> cell | other |\n\n");
    CHECK(seed_of("> ## quoted heading") == "> quoted heading\n\n");
    CHECK(seed_of("**bold** and `code`") == "> **bold** and `code`\n\n");
    CHECK(seed_of("#hashtag not a heading") == "> #hashtag not a heading\n\n");
    CHECK(seed_of("*emphasis* not a bullet") ==
          "> *emphasis* not a bullet\n\n");
}

static void a_horizontal_rule_leaves_no_words_so_it_is_passed_over() {
    CHECK(seed_of("---\nthe words after it") == "> the words after it\n\n");
}

static void runs_of_whitespace_collapse_to_one_space() {
    CHECK(seed_of("|  padded   |  table  |") == "> padded | table |\n\n");
}

static void the_opener_is_cut_at_a_word_boundary_with_an_ellipsis() {
    const std::string long_prose =
        "the reconciliation ledger disagrees with the bank feed every single "
        "morning";
    const std::string quoted = seed_of(long_prose);
    CHECK(quoted == "> the reconciliation ledger disagrees with the bank "
                    "feed\xe2\x80\xa6\n\n");
}

static void an_opener_at_the_cap_is_not_cut_at_all() {
    const std::string exact(rq::kOpenerCap, 'x');
    CHECK(seed_of(exact) == "> " + exact + "\n\n");
}

static void one_long_token_is_cut_through_rather_than_handed_back_short() {
    const std::string hash(80, 'a');
    const std::string quoted = seed_of("see " + hash);
    CHECK(quoted == "> see " + std::string(rq::kOpenerCap - 4, 'a') +
                        "\xe2\x80\xa6\n\n");
}

static void a_message_with_no_ink_offers_nothing() {
    CHECK(!rq::is_offered("   \n\t\n", false));
    CHECK(seed_of("   \n\t\n").empty());
    CHECK(!rq::has_ink("\xe2\x80\x8b"));
    CHECK(seed_of("\xe2\x80\x8b").empty());
}

static void an_attachment_only_message_quotes_what_it_carried() {
    CHECK(rq::is_offered("", true));
    CHECK(seed_of("", {"shot.png", "notes.md"}) ==
          "> shot.png, notes.md\n\n");
    CHECK(seed_of("  ", {"shot.png"}) == "> shot.png\n\n");
}

static void a_drawn_button_is_never_a_dead_one() {
    const char* messages[] = {
        "plain",     "```\ncode\n```", "```", "---", "   ",
        "> quoted",  "# heading",      "",    "\xe2\x80\x8b",
    };
    for (const char* m : messages) {
        const bool offered = rq::is_offered(m, false);
        const bool seeded = rq::seed(m).has_value();
        CHECK(offered == seeded);
    }
}

static void the_first_character_survives_when_it_is_not_ascii() {
    CHECK(seed_of("\xf0\x9f\x91\x8d ship it") ==
          "> \xf0\x9f\x91\x8d ship it\n\n");
}

int main() {
    a_quote_is_the_marker_the_opener_and_a_blank_line();
    the_opener_is_the_first_line_only();
    a_fence_is_skipped_whole_and_the_prose_under_it_is_quoted();
    an_unclosed_fence_falls_back_to_the_code_inside_it();
    a_message_that_is_only_a_fence_marker_still_offers_a_quote();
    block_markers_come_off_and_inline_markers_stay_on();
    a_horizontal_rule_leaves_no_words_so_it_is_passed_over();
    runs_of_whitespace_collapse_to_one_space();
    the_opener_is_cut_at_a_word_boundary_with_an_ellipsis();
    an_opener_at_the_cap_is_not_cut_at_all();
    one_long_token_is_cut_through_rather_than_handed_back_short();
    a_message_with_no_ink_offers_nothing();
    an_attachment_only_message_quotes_what_it_carried();
    a_drawn_button_is_never_a_dead_one();
    the_first_character_survives_when_it_is_not_ascii();

    if (g_failures > 0) {
        std::printf("test_reply_quote: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_reply_quote: all checks passed\n");
    return 0;
}
