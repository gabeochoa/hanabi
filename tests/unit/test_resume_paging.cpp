#include <cstdio>

#include "../../src/api/resume_paging.h"

namespace rp = hanabi::api::resume_paging;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_resume_paging ===\n");
    CHECK(rp::page_limit(0, 40, true) == 40);
    CHECK(rp::page_limit(1, 40, true) == 40);
    CHECK(rp::page_limit(5, 40, true) == 40);
    CHECK(rp::page_limit(0, 40, false) == 40);
    CHECK(rp::page_limit(0, 10, true) == 10);
    CHECK(rp::page_limit(0, 0, true) == 500);
    CHECK(rp::page_limit(1, 900, true) == 500);
    CHECK(rp::after_page(true, 76, 95) == rp::Next::Reached);
    CHECK(rp::after_page(true, 61, 95) == rp::Next::Reached);
    CHECK(rp::after_page(true, 61, 30) == rp::Next::Continue);
    CHECK(rp::after_page(true, 21, 30) == rp::Next::Reached);

    CHECK(rp::page_bound(false, true, 20) == rp::kSafetyPages);
    CHECK(rp::page_bound(true, false, 20) == 20);
    CHECK(rp::page_bound(false, false, 20) == 1);

    CHECK(rp::after_page(true, 0, 50) == rp::Next::NoCursor);
    CHECK(rp::after_page(true, 51, 50) == rp::Next::Continue);
    CHECK(rp::after_page(true, 50, 50) == rp::Next::Reached);
    CHECK(rp::after_page(true, 12, 50) == rp::Next::Reached);
    CHECK(rp::after_page(false, 12, 50) == rp::Next::Continue);

    const rp::GapOutcome none = rp::on_page_error(true, 0, 999);
    CHECK(!none.keepPages && !none.hasMoreOlder);
    const rp::GapOutcome kept = rp::on_page_error(true, 2, 777);
    CHECK(kept.keepPages && kept.hasMoreOlder);
    const rp::GapOutcome plain = rp::on_page_error(false, 2, 777);
    CHECK(!plain.keepPages);

    CHECK(!rp::has_more_older(true, true, true, true));
    CHECK(rp::has_more_older(true, true, false, true));
    CHECK(rp::has_more_older(true, false, true, true));
    CHECK(rp::has_more_older(true, false, false, false));
    CHECK(!rp::has_more_older(false, false, false, true));
    CHECK(rp::has_more_older(false, false, true, false));

    CHECK(rp::gap_closed(false, 0, 0));
    CHECK(rp::gap_closed(true, 50, 50));
    CHECK(!rp::gap_closed(true, 51, 50));
    CHECK(!rp::gap_closed(true, 0, 50));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
