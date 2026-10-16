#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/read_cursor.h"

namespace rc = hanabi::read_cursor;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

struct M { std::string id; };

int main() {
    std::printf("=== test_read_cursor ===\n");
    CHECK(rc::seq_of("42") == 42);
    CHECK(rc::seq_of("0") == 0);
    CHECK(rc::seq_of("t2-u3") == 0);
    CHECK(rc::seq_of("") == 0);
    CHECK(rc::seq_of("run_finished:3") == 0);
    CHECK(rc::seq_of("12345678901234567890") == 0);
    CHECK(rc::ids_are_seqs("1", "9") && !rc::ids_are_seqs("m1", "9"));
    CHECK(rc::unread_count(10, 7) == 3);
    CHECK(rc::unread_count(10, 10) == 0 && rc::unread_count(10, 12) == 0);
    CHECK(rc::unread_count(10, 0) == 0 && rc::unread_count(0, 3) == 0);
    CHECK(rc::unread_count(5000, 1) == 999);
    CHECK(rc::unread_label(0).empty() && rc::unread_label(3) == "3" && rc::unread_label(999) == "999+");
    const std::vector<M> msgs{{"3"}, {"4"}, {"7"}, {"run_finished:1"}, {"9"}};
    CHECK(rc::first_unread_by_seq(msgs, 4) == 2);
    CHECK(rc::first_unread_by_seq(msgs, 9) == -1);
    CHECK(rc::first_unread_by_seq(msgs, 0) == -1);
    CHECK(rc::first_unread_by_seq(msgs, 1) == 0);
    CHECK(rc::newest_seq(msgs) == 9);
    CHECK(rc::newest_seq(std::vector<M>{{"m1"}}) == 0);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
