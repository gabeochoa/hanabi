#include <cstdio>
#include <string>

#include "../../src/ui/thinking_tags.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

static void test_thinking_tags_unwrap_for_display_only_by_the_reference_grammar() {
    std::printf("test_thinking_tags_unwrap_for_display_only_by_the_reference_grammar\n");
    namespace tt = hanabi::thinking_tags;
    CHECK(tt::unwrap("<thinking>plan</thinking>\n\nAnswer") == "plan\n\nAnswer");
    CHECK(tt::unwrap("Answer <thinking>x</thinking> more") == "Answer x more");
    CHECK(tt::unwrap("<Thinking>a</THINKING>") == "a");
    CHECK(tt::unwrap("<thinking>a<thinking>b</thinking>c</thinking>") == "abc");
    CHECK(tt::is_blank(tt::unwrap("<thinking></thinking>")));
    CHECK(tt::is_blank(tt::unwrap("<thinking>\n\n</thinking>")));
    CHECK(tt::is_blank(tt::unwrap("   ")));
    CHECK(tt::unwrap("<thinking>never closed") == "never closed");
    CHECK(tt::unwrap("  \n<thinking>never closed") == "  \nnever closed");
    CHECK(tt::unwrap("text <thinking>never closed") == "text <thinking>never closed");
    CHECK(tt::unwrap("</thinking>tail") == "tail");
    CHECK(tt::unwrap("text </thinking>") == "text </thinking>");
    CHECK(tt::unwrap("<thinking foo=\"1\">b</thinking>") == "<thinking foo=\"1\">b</thinking>");
    CHECK(tt::unwrap("<thinking foo=\"1\">never closed") == "<thinking foo=\"1\">never closed");
    CHECK(tt::unwrap("`<thinking>` tag") == "`<thinking>` tag");
    CHECK(tt::unwrap("> <thinking>q</thinking>") == "> <thinking>q</thinking>");
    CHECK(tt::unwrap("\\<thinking>e</thinking>") == "\\<thinking>e</thinking>");
    CHECK(tt::unwrap("```\n<thinking>code</thinking>\n```") == "```\n<thinking>code</thinking>\n```");
    CHECK(tt::unwrap("<thinking>a</thinking>\n```\n<thinking>c</thinking>\n```\n<thinking>b</thinking>") ==
          "a\n```\n<thinking>c</thinking>\n```\nb");
    CHECK(tt::unwrap("<thinkingfoo>x</thinkingfoo>") == "<thinkingfoo>x</thinkingfoo>");
    CHECK(tt::unwrap("<thinking") == "<thinking");
    CHECK(tt::unwrap("plain") == "plain");
    CHECK(tt::unwrap("").empty());
}

int main() {
    std::printf("=== test_thinking_tags ===\n");
    test_thinking_tags_unwrap_for_display_only_by_the_reference_grammar();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
