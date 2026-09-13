// A message that could not be sent, and what the person can still do with it.
//
// The contract, held without a window: acknowledging is by the message's own
// local_id and survives a restart; acknowledging never touches the record's
// text or its attachments; and two records are two notices, so quieting one
// does not silence the other.

#include <cstdio>
#include <string>

#include "../../src/util/acknowledged.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    namespace M = hanabi::model;
    std::vector<std::string> seen;

    // Nothing is acknowledged to begin with, and an empty id never is: a
    // record without a local_id fails OPEN, so it keeps being offered
    // rather than being quietly swallowed.
    CHECK(!M::acknowledged(seen, "m-one"));
    CHECK(!M::acknowledged(seen, ""));
    CHECK(!M::acknowledge(seen, ""));
    CHECK(seen.empty());

    // Two records are two notices: acknowledging one leaves the other
    // asking, even when their text is identical -- the key is the
    // message's own local_id.
    CHECK(M::acknowledge(seen, "m-one"));
    CHECK(M::acknowledged(seen, "m-one"));
    CHECK(!M::acknowledged(seen, "m-two"));
    CHECK(M::acknowledge(seen, "m-two"));
    CHECK(seen.size() == 2);

    // Saying it twice changes nothing, and says so, so a caller that
    // persists on change does not write the same file again.
    CHECK(!M::acknowledge(seen, "m-one"));
    CHECK(seen.size() == 2);

    // A restart is this list coming back: what was acknowledged stays
    // acknowledged, and what was not is still offered.
    const std::vector<std::string> restored = seen;
    CHECK(M::acknowledged(restored, "m-one"));
    CHECK(M::acknowledged(restored, "m-two"));
    CHECK(!M::acknowledged(restored, "m-three"));

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
