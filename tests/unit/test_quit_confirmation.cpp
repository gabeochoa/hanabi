#include <cstdio>

#include "../../src/quit_confirmation.h"

namespace qc = hanabi::quit_confirmation;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_quit_confirmation ===\n");
    CHECK(qc::invocation(true, true, "q") == qc::Invocation::QuitShortcut);
    CHECK(qc::invocation(true, true, "Q") == qc::Invocation::QuitShortcut);
    CHECK(qc::invocation(false, true, "q") == qc::Invocation::Explicit);
    CHECK(qc::invocation(true, false, "q") == qc::Invocation::Explicit);
    CHECK(qc::invocation(true, true, "w") == qc::Invocation::Explicit);
    CHECK(qc::invocation(true, true, "") == qc::Invocation::Explicit);
    CHECK(qc::invocation(false, false, "") == qc::Invocation::Explicit);

    CHECK(qc::should_ask(true, qc::Invocation::QuitShortcut));
    CHECK(!qc::should_ask(true, qc::Invocation::Explicit));
    CHECK(!qc::should_ask(false, qc::Invocation::QuitShortcut));
    CHECK(!qc::should_ask(false, qc::Invocation::Explicit));

    CHECK(qc::should_disable(qc::Answer::Quit, true));
    CHECK(!qc::should_disable(qc::Answer::Quit, false));
    CHECK(!qc::should_disable(qc::Answer::Cancel, true));
    CHECK(!qc::should_disable(qc::Answer::Cancel, false));

    CHECK(qc::answer_for_button(0) == qc::Answer::Quit);
    CHECK(qc::answer_for_button(1) == qc::Answer::Cancel);
    CHECK(qc::answer_for_button(-1) == qc::Answer::Cancel);
    CHECK(qc::answer_for_button(7) == qc::Answer::Cancel);
    CHECK(qc::kButtonCount == 2);

    CHECK(qc::title("Hanabi") == "Quit Hanabi?");
    CHECK(qc::body("Hanabi") == "Are you sure you want to quit Hanabi? All its windows will close.");
    CHECK(qc::kQuitButton == "Quit" && qc::kCancelButton == "Cancel" && qc::kSuppressionTitle == "Don't ask again");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
