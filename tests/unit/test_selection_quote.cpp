#include <cstdio>
#include <string>

#include "../../src/ui/selection_quote.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sq = hanabi::selection_quote;

int main() {
    // Prose is quoted, line by line.
    CHECK(sq::block("4,810 match to the cent") == "> 4,810 match to the cent");
    CHECK(sq::block("one\ntwo") == "> one\n> two");
    // Code is fenced; indentation survives; a fence inside cannot close it.
    CHECK(sq::looks_like_code("if (x) {\n    y();\n}"));
    CHECK(sq::block("int a = f(b);") == "```\nint a = f(b);\n```");
    CHECK(sq::block("```py\nx\n```\n  y") == "````\n```py\nx\n```\n  y\n````");
    CHECK(!sq::looks_like_code("Reconciled 4,812 accounts."));
    // Appended after the draft with a blank line; an empty draft starts with it.
    CHECK(sq::appended("about this:", "a b") == "about this:\n\n> a b\n\n");
    CHECK(sq::appended("", "a b") == "> a b\n\n");
    CHECK(sq::appended("keep me", "\n\n") == "keep me");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
