// The compaction divider's words (src/api/compaction.h).
//
// One row, two states, and every number in it is either the server's or a
// formatting of the server's. The formats are pinned here against the values
// the reference draws for the same inputs -- "3m 01s", "9.9k", "12k", "1M" --
// because a divider that reads "3m 1s" beside a web page reading "3m 01s" is
// the kind of difference a reader notices and a pixel metric cannot.
#include <cstdio>
#include <string>

#include "../../src/api/compaction.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        const std::string _a = (a);                                          \
        const std::string _b = (b);                                          \
        if (_a != _b) {                                                      \
            std::printf("  FAIL: %s == %s -> got \"%s\", want \"%s\" (line " \
                        "%d)\n",                                             \
                        #a, #b, _a.c_str(), _b.c_str(), __LINE__);           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

using namespace api::compaction;

static void test_durations_pad_their_trailing_part() {
    CHECK_EQ(duration_label(0), "0ms");
    CHECK_EQ(duration_label(999), "999ms");
    CHECK_EQ(duration_label(1000), "1s");
    CHECK_EQ(duration_label(59999), "59s");
    CHECK_EQ(duration_label(60000), "1m 00s");
    CHECK_EQ(duration_label(181000), "3m 01s");
    CHECK_EQ(duration_label(181999), "3m 01s");
    CHECK_EQ(duration_label(3599000), "59m 59s");
    CHECK_EQ(duration_label(3600000), "1h 00m");
    CHECK_EQ(duration_label(4020000), "1h 07m");
    // A negative reading is a clock skew, not a negative duration.
    CHECK_EQ(duration_label(-5), "0ms");
}

static void test_token_counts_round_where_the_reference_rounds() {
    CHECK_EQ(compact_tokens(0), "0");
    CHECK_EQ(compact_tokens(-1), "0");
    CHECK_EQ(compact_tokens(999), "999");
    CHECK_EQ(compact_tokens(1000), "1k");
    CHECK_EQ(compact_tokens(1500), "1.5k");
    CHECK_EQ(compact_tokens(9900), "9.9k");
    CHECK_EQ(compact_tokens(9950), "10k");  // >= 9.95 drops the decimal
    CHECK_EQ(compact_tokens(12000), "12k");
    CHECK_EQ(compact_tokens(12400), "12k");
    CHECK_EQ(compact_tokens(999499), "999k");
    CHECK_EQ(compact_tokens(999500), "1M");  // rounds up past the unit
    CHECK_EQ(compact_tokens(1500000), "1.5M");
    CHECK_EQ(compact_tokens(2000000000), "2B");
}

static void test_the_running_label_says_only_what_the_wire_carried() {
    Progress none;
    CHECK_EQ(running_label(none, 1000), kRunningLabel);
    CHECK_EQ(detail(none, 1000), "");

    Progress anchored;
    anchored.started_at_unix_ms = 1000000;
    CHECK_EQ(running_label(anchored, 1000000 + 181000),
             std::string(kRunningLabel) + " 3m 01s");

    Progress read;
    read.output_tokens = 9900;
    CHECK_EQ(running_label(read, 5), std::string(kRunningLabel) + " 9.9k tokens");

    Progress both;
    both.started_at_unix_ms = 1000000;
    both.output_tokens = 9900;
    CHECK_EQ(running_label(both, 1000000 + 181000),
             std::string(kRunningLabel) + " 3m 01s \xc2\xb7 9.9k tokens");

    // The server's clock ahead of ours reads as zero, never negative.
    CHECK(elapsed_ms(both, 999000) == 0);
    CHECK(elapsed_ms(none, 999000) == -1);
    // A zero reading is a reading; only the absence is silent.
    Progress zero;
    zero.output_tokens = 0;
    CHECK_EQ(detail(zero, 5), "0 tokens");
}

int main() {
    std::printf("== test_compaction (divider labels) ==\n");
    test_durations_pad_their_trailing_part();
    test_token_counts_round_where_the_reference_rounds();
    test_the_running_label_says_only_what_the_wire_carried();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
