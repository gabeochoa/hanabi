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
    CHECK(sc::classify(false, false, false) == sc::Chord::None);
    CHECK(sc::classify(false, true, false) == sc::Chord::None);
    CHECK(sc::classify(true, false, false) == sc::Chord::SendNow);
    CHECK(sc::classify(true, false, true) == sc::Chord::None);
    CHECK(sc::classify(true, true, false) == sc::Chord::QueueThisOne);
    CHECK(sc::classify(true, true, true) == sc::Chord::QueueThisOne);
    const auto idle = sc::hint_rows(false, false);
    CHECK(idle.size() == 1 && idle[0].chord == "Return" && idle[0].meaning == "Send");
    const auto running = sc::hint_rows(true, false);
    CHECK(running.size() == 3);
    CHECK(running[0].chord == "Return" && running[0].meaning == "Steer the running turn");
    CHECK(running[1].chord == "Cmd Opt Return" && running[1].meaning == "Queue this one instead");
    CHECK(running[2].chord == "Cmd Return" && running[2].meaning == "Send now");
    const auto queued = sc::hint_rows(true, true);
    CHECK(queued[0].meaning == "Queue for the end of the turn" && queued[1].meaning == "Steer this one instead");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
