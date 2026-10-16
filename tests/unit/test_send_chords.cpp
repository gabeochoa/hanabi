#include <cstdio>
#include <string>

#include "../../src/ui/send_chords.h"

namespace sc = hanabi::send_chords;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_send_chords ===\n");
    CHECK(sc::classify(false, false) == sc::Chord::None);
    CHECK(sc::classify(false, true) == sc::Chord::None);
    CHECK(sc::classify(true, false) == sc::Chord::None);
    CHECK(sc::classify(true, true) == sc::Chord::QueueThisOne);
    const auto idle = sc::hint_rows(false);
    CHECK(idle.size() == 1 && idle[0].chord == "Return" && idle[0].meaning == "Send");
    const auto running = sc::hint_rows(true);
    CHECK(running.size() == 2);
    CHECK(running[0].chord == "Return" && running[0].meaning == "Steer this one");
    CHECK(running[1].chord == "Cmd Opt Return" && running[1].meaning == "Queue this one");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
