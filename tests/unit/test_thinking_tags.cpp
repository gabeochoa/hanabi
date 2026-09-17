// The display-only unwrapping of <thinking> protocol tags: std-only, so it
// runs anywhere the header compiles. The vector is the reference's grammar,
// moved unchanged from test_data.
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
    // Paired plain tags go, the text between stays.
    CHECK(tt::unwrap("<thinking>plan</thinking>\n\nAnswer") == "plan\n\nAnswer");
    CHECK(tt::unwrap("Answer <thinking>x</thinking> more") == "Answer x more");
    CHECK(tt::unwrap("<Thinking>a</THINKING>") == "a");
    CHECK(tt::unwrap("<thinking>a<thinking>b</thinking>c</thinking>") == "abc");
    // Empty / whitespace-only wrappers render to nothing.
    CHECK(tt::is_blank(tt::unwrap("<thinking></thinking>")));
    CHECK(tt::is_blank(tt::unwrap("<thinking>\n\n</thinking>")));
    CHECK(tt::is_blank(tt::unwrap("   ")));
    // Unmatched plain tags: removed only at the text's first non-space offset.
    CHECK(tt::unwrap("<thinking>never closed") == "never closed");
    CHECK(tt::unwrap("  \n<thinking>never closed") == "  \nnever closed");
    CHECK(tt::unwrap("text <thinking>never closed") == "text <thinking>never closed");
    CHECK(tt::unwrap("</thinking>tail") == "tail");
    CHECK(tt::unwrap("text </thinking>") == "text </thinking>");
    // Attributed tags claim their partner; the pair survives whole.
    CHECK(tt::unwrap("<thinking foo=\"1\">b</thinking>") == "<thinking foo=\"1\">b</thinking>");
    CHECK(tt::unwrap("<thinking foo=\"1\">never closed") == "<thinking foo=\"1\">never closed");
    // Sheltered: code span, blockquote line, backslash escape, fence.
    CHECK(tt::unwrap("`<thinking>` tag") == "`<thinking>` tag");
    CHECK(tt::unwrap("> <thinking>q</thinking>") == "> <thinking>q</thinking>");
    CHECK(tt::unwrap("\\<thinking>e</thinking>") == "\\<thinking>e</thinking>");
    CHECK(tt::unwrap("```\n<thinking>code</thinking>\n```") == "```\n<thinking>code</thinking>\n```");
    CHECK(tt::unwrap("<thinking>a</thinking>\n```\n<thinking>c</thinking>\n```\n<thinking>b</thinking>") ==
          "a\n```\n<thinking>c</thinking>\n```\nb");
    // Not the tag: a longer name, no closing '>'.
    CHECK(tt::unwrap("<thinkingfoo>x</thinkingfoo>") == "<thinkingfoo>x</thinkingfoo>");
    CHECK(tt::unwrap("<thinking") == "<thinking");
    // Text without tags is returned as is.
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
