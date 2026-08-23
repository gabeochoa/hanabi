// Reading a unified diff, line by line (src/util/diff.h).
//
// The renderer only picks colours from these answers, so what is worth pinning
// is the reading itself: which line is an addition, which is a file header
// that merely looks like one, and — the case that decides whether this feature
// is safe to run over every tool's output — which text is not a diff at all.
#include <cstdio>
#include <string>

#include "../../src/util/diff.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::diff::LineKind;
using hanabi::diff::classify;
using hanabi::diff::looks_like_diff;

static void test_the_four_kinds_of_line() {
    std::printf("test_the_four_kinds_of_line\n");
    CHECK(classify("+ let delay = full_jitter(base);") == LineKind::Added);
    CHECK(classify("+++more plus signs") == LineKind::Added);
    CHECK(classify("- let delay = Duration::from_millis(200);") ==
          LineKind::Removed);
    CHECK(classify("@@ -42,7 +42,9 @@ fn retry()") == LineKind::Hunk);
    CHECK(classify("  unchanged line") == LineKind::Context);
    CHECK(classify("") == LineKind::Context);
    CHECK(classify("34 insertions(+), 8 deletions(-)") == LineKind::Context);
}

static void test_file_headers_are_not_changes() {
    std::printf("test_file_headers_are_not_changes\n");
    // "--- a/foo.rs" starts with three deletions if you read it carelessly,
    // and the whole header block would come out red.
    CHECK(classify("--- a/worker/sync_loop.rs") == LineKind::Meta);
    CHECK(classify("+++ b/worker/sync_loop.rs") == LineKind::Meta);
    CHECK(classify("diff --git a/x.rs b/x.rs") == LineKind::Meta);
    CHECK(classify("index 9a1b2c3..4d5e6f7 100644") == LineKind::Meta);
    // A bare "---" is a horizontal rule, not a file header: no space after it.
    CHECK(classify("---") == LineKind::Removed);
}

static void test_a_diff_says_it_is_one() {
    std::printf("test_a_diff_says_it_is_one\n");
    CHECK(looks_like_diff("@@ worker/sync_loop.rs\n- old\n+ new\n"));
    CHECK(looks_like_diff("@@ -1,3 +1,4 @@"));
    // The header pair is the other structural marker, and it takes BOTH.
    CHECK(looks_like_diff("--- a/x.rs\n+++ b/x.rs\n+ added\n"));
    CHECK(!looks_like_diff("--- a/x.rs\nnothing else\n"));
}

static void test_ordinary_output_is_not_a_diff() {
    std::printf("test_ordinary_output_is_not_a_diff\n");
    // The reason `looks_like_diff` demands a structural marker. Every one of
    // these is something a tool really prints, and every one of them would be
    // painted red or green by a leading-character vote.
    CHECK(!looks_like_diff("- fix the retry loop\n- add a test\n- ship it"));
    CHECK(!looks_like_diff("usage: hanabi [-v] [--help]\n  -v  be loud"));
    CHECK(!looks_like_diff("+---------+--------+\n| attempt | delay  |"));
    CHECK(!looks_like_diff("total 8\ndrwxr-xr-x  4 gabe  staff  128 Aug 23"));
    CHECK(!looks_like_diff(""));
}

int main() {
    std::printf("=== test_diff ===\n");
    test_the_four_kinds_of_line();
    test_file_headers_are_not_changes();
    test_a_diff_says_it_is_one();
    test_ordinary_output_is_not_a_diff();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
