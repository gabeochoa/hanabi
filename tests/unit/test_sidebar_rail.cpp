#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/sidebar_rail.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace rail = ecs::rail;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    std::printf("=== test_sidebar_rail ===\n");

    // The reference's numbers (D122996657, D122996665): a 10px dot on a 19px
    // row, 10px arrow slots.
    CHECK(rail::kDotDiameter == 10.0f && rail::kRowH == 19.0f && rail::kArrowSlotH == 10.0f);
    CHECK(near(rail::dot_center(0), 9.5f) && near(rail::dot_center(3), 66.5f));
    CHECK(near(rail::content_h(30), 570.0f));

    // Which threads get a dot: what Home lists.
    {
        api::SessionSummary s;
        s.id = "a";
        s.title = "Fix the build";
        s.title_is_human = true;
        CHECK(rail::shows(s, false));
        api::SessionSummary archived = s;
        archived.archive_override = true;
        CHECK(!rail::shows(archived, false));
        api::SessionSummary child = s;
        child.parent_id = "p";
        CHECK(!rail::shows(child, false));
        api::SessionSummary bot = s;
        bot.origin_application = "metamate";
        bot.title_is_human = false;
        CHECK(api::is_automation_born(bot));
        CHECK(!rail::shows(bot, false));
        CHECK(rail::shows(bot, true));       // the reader asked to see automations
        bot.starred = true;
        CHECK(rail::shows(bot, false));      // a pin is a recorded choice
    }

    // Scrolling: arrows only on a side with dots out of view; a page is a
    // screenful less one row, never past either end.
    {
        rail::Scroll s{0.0f, 190.0f, rail::content_h(30)};
        CHECK(!s.hides_above() && s.hides_below());
        CHECK(near(s.page_target(true), 171.0f));
        CHECK(near(s.page_target(false), 0.0f));
        s.top = s.max_top();
        CHECK(near(s.top, 380.0f));
        CHECK(s.hides_above() && !s.hides_below());
        CHECK(near(s.page_target(true), 380.0f));
        CHECK(near(s.page_target(false), 209.0f));
        rail::Scroll fits{0.0f, 400.0f, rail::content_h(5)};
        CHECK(!fits.hides_above() && !fits.hides_below());
        CHECK(near(fits.page_target(true), 0.0f));
        CHECK(near(fits.clamp(50.0f), 0.0f));
        rail::Scroll tiny{0.0f, 10.0f, rail::content_h(5)};
        CHECK(near(tiny.page_target(true), 19.0f));  // at least one row
    }

    // Hit rows and visibility under a scroll.
    {
        rail::Scroll s{38.0f, 95.0f, rail::content_h(10)};
        CHECK(rail::row_at(s, 0.0f, 10) == 2);
        CHECK(rail::row_at(s, 18.9f, 10) == 2);
        CHECK(rail::row_at(s, 19.0f, 10) == 3);
        CHECK(rail::row_at(s, -1.0f, 10) == -1);
        CHECK(rail::row_at(s, 95.0f, 10) == -1);
        rail::Scroll end{0.0f, 400.0f, rail::content_h(3)};
        CHECK(rail::row_at(end, 60.0f, 3) == -1);  // below the last dot
        CHECK(!rail::row_visible(s, 1) && rail::row_visible(s, 2) && rail::row_visible(s, 6) &&
              !rail::row_visible(s, 7));
    }

    // The card: arrow level with the title when there is room; near the
    // bottom it slides up and keeps its arrow on the dot.
    {
        const auto p = rail::card_placement(200.0f, 80.0f, 0.0f, 700.0f);
        CHECK(near(p.top, 182.0f) && near(p.arrowY, 18.0f));
        const auto low = rail::card_placement(690.0f, 80.0f, 0.0f, 700.0f);
        CHECK(near(low.top, 616.0f) && near(low.arrowY, 74.0f));
        const auto high = rail::card_placement(10.0f, 80.0f, 0.0f, 700.0f);
        CHECK(near(high.top, 4.0f) && near(high.arrowY, 6.0f));
        CHECK(near(rail::arrow_tip(6.0f, 80.0f), 11.0f));
        CHECK(near(rail::arrow_tip(78.0f, 80.0f), 69.0f));
        CHECK(near(rail::arrow_tip(30.0f, 80.0f), 30.0f));
    }

    // Hover: 0.2 s to open, an open card follows at once, 0.12 s grace to close.
    {
        rail::Hover h;
        h.step("a", 0.0);
        CHECK(h.shown.empty());
        h.step("a", 0.19);
        CHECK(h.shown.empty());
        h.step("a", 0.2);
        CHECK(h.shown == "a");
        h.step("b", 0.3);
        CHECK(h.shown == "b");
        h.step("", 0.4);
        CHECK(h.shown == "b");
        h.step("", 0.51);
        CHECK(h.shown == "b");
        h.step("", 0.52);
        CHECK(h.shown.empty());
        // Leaving and coming back inside the grace keeps the card up.
        h.step("c", 1.0);
        h.step("c", 1.2);
        CHECK(h.shown == "c");
        h.step("", 1.25);
        h.step("d", 1.3);
        CHECK(h.shown == "d");
        // A brush across a dot shorter than the delay opens nothing.
        h.close();
        h.step("e", 2.0);
        h.step("", 2.1);
        h.step("", 2.5);
        CHECK(h.shown.empty());
    }

    // The last message you sent: newest User message with text.
    {
        std::vector<api::Message> ms;
        CHECK(rail::last_sent(ms).empty());
        ms.emplace_back("1", api::Role::User, "first ask");
        ms.emplace_back("2", api::Role::Assistant, "an answer");
        ms.emplace_back("3", api::Role::User, "  \n ");
        ms.emplace_back("4", api::Role::Tool, "tool output");
        CHECK(rail::last_sent(ms) == "first ask");
        ms.emplace_back("5", api::Role::User, "second ask");
        CHECK(rail::last_sent(ms) == "second ask");
    }

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
