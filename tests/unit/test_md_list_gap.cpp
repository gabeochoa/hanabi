#include <cstdio>
#include <string>

#include "../../src/ui/md_list_gap.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using hanabi::md::with_list_paragraph_gaps;

int main() {
    CHECK(with_list_paragraph_gaps("- a\n- b\nAfter.") == "- a\n- b\n\nAfter.");
    CHECK(with_list_paragraph_gaps("1. a\n2) b\nAfter.") == "1. a\n2) b\n\nAfter.");
    CHECK(with_list_paragraph_gaps("- a\n\nAfter.") == "- a\n\nAfter.");        // already a gap
    CHECK(with_list_paragraph_gaps("- a\n  more of a\n- b") == "- a\n  more of a\n- b");  // continuation
    CHECK(with_list_paragraph_gaps("- a\n  more\nAfter.") == "- a\n  more\n\nAfter.");
    CHECK(with_list_paragraph_gaps("Before.\n- a") == "Before.\n- a");            // only BELOW a list
    CHECK(with_list_paragraph_gaps("```\n- a\nx\n```") == "```\n- a\nx\n```");   // fences untouched
    CHECK(with_list_paragraph_gaps("\xe2\x80\xa2  a\nAfter.") == "\xe2\x80\xa2  a\n\nAfter.");
    CHECK(with_list_paragraph_gaps("Plain\ntext") == "Plain\ntext");
    CHECK(with_list_paragraph_gaps("3.5 is a number\nnext") == "3.5 is a number\nnext");
    {
        std::string storage;
        const std::string plain = "Plain\n- a\n  b";
        CHECK(&hanabi::md::list_gapped(plain, storage) == &plain);  // no copy when nothing is owed
        const std::string owed = "- a\nAfter.";
        CHECK(hanabi::md::list_gapped(owed, storage) == "- a\n\nAfter.");
        const std::string fenced = "```\n- a\nx\n```";
        CHECK(hanabi::md::list_gapped(fenced, storage) == fenced);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
