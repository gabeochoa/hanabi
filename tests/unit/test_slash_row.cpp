#include "../../src/ui/slash_row.h"

#include <cstdio>

using hanabi::slash_row::Slots;
using hanabi::slash_row::budget;
using hanabi::slash_row::kBlurbMin;
using hanabi::slash_row::kCommandMin;
using hanabi::slash_row::kCommandW;
using hanabi::slash_row::kStatusShortW;
using hanabi::slash_row::kStatusW;
using hanabi::slash_row::status_label;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void fits(float row, bool runnable) {
    const Slots s = budget(row, runnable);
    if (s.total() > row + 0.001f)
        std::printf("  FAIL: %.0f (%s) children total %.0f > row\n", row,
                    runnable ? "runnable" : "unavailable", s.total());
    CHECK(s.total() <= row + 0.001f);
    CHECK(s.command >= 0.0f);
    CHECK(s.blurb >= 0.0f);
    CHECK(s.status >= 0.0f);
}

int main() {
    // Nothing may ever leave the row, at any width the window can produce.
    for (float row = 0.0f; row <= 1400.0f; row += 1.0f) {
        fits(row, true);
        fits(row, false);
    }

    const Slots wide = budget(699.0f, true);
    CHECK(wide.command == kCommandW);
    CHECK(wide.blurb > kBlurbMin);
    CHECK(wide.status == 0.0f);

    const Slots wideStatus = budget(699.0f, false);
    CHECK(wideStatus.command == kCommandW);
    CHECK(wideStatus.status == kStatusW);
    CHECK(wideStatus.blurb > kBlurbMin);

    // The split row that shipped the defect: 175pt.
    const Slots split = budget(175.0f, true);
    CHECK(split.command == kCommandW);
    CHECK(split.blurb == 0.0f);
    CHECK(split.status == 0.0f);

    // Unavailable in that same split row borrows from the command rather than
    // painting its label outside the menu.
    const Slots splitStatus = budget(175.0f, false);
    CHECK(splitStatus.status == kStatusW);
    CHECK(splitStatus.command >= kCommandMin);
    CHECK(splitStatus.command < kCommandW);
    CHECK(splitStatus.blurb == 0.0f);
    CHECK(status_label(splitStatus.status) == "Unavailable");

    // The budget is against the row's CONTENT box, which option_row insets by
    // kRowInset in total -- the outer width would let every child overrun.
    const Slots wideRow = budget(699.0f, true);
    CHECK(wideRow.total() <= 699.0f);
    CHECK(hanabi::slash_row::kPad + wideRow.command + wideRow.blurb +
              wideRow.status <=
          699.0f - hanabi::slash_row::kRowInset);

    // A blurb is either readable or absent; a 6-character sliver is neither.
    for (float row = 0.0f; row <= 1400.0f; row += 1.0f) {
        const Slots s = budget(row, true);
        CHECK(s.blurb == 0.0f || s.blurb >= kBlurbMin);
        const Slots u = budget(row, false);
        CHECK(u.blurb == 0.0f || u.blurb >= kBlurbMin);
        CHECK(u.status == 0.0f || u.status >= kStatusShortW);
    }

    const Slots none = budget(20.0f, false);
    CHECK(none.total() <= 20.0f + 0.001f);
    const Slots empty = budget(0.0f, true);
    CHECK(empty.command == 0.0f);
    CHECK(empty.blurb == 0.0f);
    CHECK(empty.status == 0.0f);

    CHECK(status_label(kStatusW) == "Unavailable");
    CHECK(status_label(kStatusShortW) == "n/a");

    if (failures != 0) {
        std::printf("slash row: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("slash row: ok\n");
    return 0;
}
