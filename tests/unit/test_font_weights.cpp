// The weight aliases name REAL faces. Afterhours draws `__default@semibold`
// only if a face is registered under exactly that name (resolve_weighted),
// and it never synthesizes a weight -- so the app's plan has to hand every
// alias a real file, fall down a ladder to the nearest lighter face the family
// actually has, and say when it did. These tests drive the plan the
// registration uses (plan_aliases is what apply() registers, row for row)
// against fake catalogs: a family with every face, one with only Medium, one
// with Regular alone, and the reader's "emphasis off" preference. The
// default-family policy is pinned the same way. What the loader then did with
// the files is the e2e probe's question (expect_font_face), in the real app.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/font_plan.h"

static int failures = 0;
#define CHECK(x)                                                        \
    do {                                                                \
        if (!(x)) {                                                     \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);    \
            ++failures;                                                 \
        }                                                               \
    } while (0)

using hanabi::fonts::AliasPlan;
using hanabi::fonts::plan_aliases;
using Faces = std::vector<std::pair<std::string, std::string>>;

static const AliasPlan* row(const std::vector<AliasPlan>& plan, const char* alias) {
    for (const auto& r : plan)
        if (r.alias == alias) return &r;
    return nullptr;
}

static void test_every_alias_gets_its_own_real_face() {
    std::printf("test_every_alias_gets_its_own_real_face\n");
    const Faces full = {{"regular", "/f/R.otf"}, {"medium", "/f/M.otf"},
                        {"semibold", "/f/SB.otf"}, {"bold", "/f/B.otf"}};
    const auto plan = plan_aliases(full, "semibold");
    CHECK(plan.size() == 5);
    const AliasPlan* base = row(plan, "__default");
    const AliasPlan* light = row(plan, "__default@light");
    const AliasPlan* medium = row(plan, "__default@medium");
    const AliasPlan* semibold = row(plan, "__default@semibold");
    const AliasPlan* bold = row(plan, "__default@bold");
    CHECK(base && base->path == "/f/R.otf" && base->weight == "regular" && !base->fallback);
    // No family hanabi knows has a light face: light IS regular, and says so.
    CHECK(light && light->path == "/f/R.otf" && light->fallback);
    CHECK(medium && medium->path == "/f/M.otf" && medium->weight == "medium" && !medium->fallback);
    CHECK(semibold && semibold->path == "/f/SB.otf" && semibold->weight == "semibold" &&
          !semibold->fallback);
    CHECK(bold && bold->path == "/f/B.otf" && bold->weight == "bold" && !bold->fallback);
    // The faces really differ: the semibold alias is not the regular file.
    CHECK(semibold->path != base->path);
}

static void test_a_missing_weight_falls_to_the_nearest_real_face_and_says_so() {
    std::printf("test_a_missing_weight_falls_to_the_nearest_real_face_and_says_so\n");
    const Faces mediumOnly = {{"regular", "/f/R.ttf"}, {"medium", "/f/M.ttf"}};
    const auto plan = plan_aliases(mediumOnly, "semibold");
    const AliasPlan* semibold = row(plan, "__default@semibold");
    const AliasPlan* bold = row(plan, "__default@bold");
    const AliasPlan* medium = row(plan, "__default@medium");
    CHECK(semibold && semibold->path == "/f/M.ttf" && semibold->weight == "medium" &&
          semibold->fallback);
    CHECK(bold && bold->path == "/f/M.ttf" && bold->weight == "medium" && bold->fallback);
    CHECK(medium && medium->path == "/f/M.ttf" && !medium->fallback);

    const Faces regularOnly = {{"regular", "/f/R.ttf"}};
    const auto bare = plan_aliases(regularOnly, "semibold");
    for (const char* alias : {"__default@medium", "__default@semibold", "__default@bold"}) {
        const AliasPlan* r = row(bare, alias);
        CHECK(r && r->path == "/f/R.ttf" && r->weight == "regular" && r->fallback);
    }
    const AliasPlan* base = row(bare, "__default");
    CHECK(base && !base->fallback);
}

static void test_emphasis_off_is_a_choice_not_a_fallback() {
    std::printf("test_emphasis_off_is_a_choice_not_a_fallback\n");
    const Faces full = {{"regular", "/f/R.otf"}, {"medium", "/f/M.otf"},
                        {"semibold", "/f/SB.otf"}, {"bold", "/f/B.otf"}};
    const auto plan = plan_aliases(full, "regular");
    for (const char* alias : {"__default@medium", "__default@semibold", "__default@bold"}) {
        const AliasPlan* r = row(plan, alias);
        CHECK(r && r->path == "/f/R.otf" && r->weight == "regular" && !r->fallback);
    }
}

static void test_the_emphasis_alias_draws_the_chosen_weight() {
    std::printf("test_the_emphasis_alias_draws_the_chosen_weight\n");
    const Faces full = {{"regular", "/f/R.otf"}, {"medium", "/f/M.otf"},
                        {"semibold", "/f/SB.otf"}, {"bold", "/f/B.otf"}};
    // The Emphasis setting is the face emphasized text draws in.
    const auto medium = plan_aliases(full, "medium");
    const AliasPlan* sb = row(medium, "__default@semibold");
    CHECK(sb && sb->path == "/f/M.otf" && sb->weight == "medium" && !sb->fallback);
    const auto bold = plan_aliases(full, "bold");
    sb = row(bold, "__default@semibold");
    CHECK(sb && sb->path == "/f/B.otf" && sb->weight == "bold" && !sb->fallback);
    // And other aliases keep their own faces regardless.
    CHECK(row(bold, "__default@medium")->path == "/f/M.otf");
}

static void test_the_default_family_is_the_reference_face_then_the_platform_then_bundled() {
    std::printf("test_the_default_family_is_the_reference_face_then_the_platform_then_bundled\n");
    CHECK(hanabi::fonts::pick_default_source(true, true) == "optimistic");
    CHECK(hanabi::fonts::pick_default_source(true, false) == "optimistic");
    CHECK(hanabi::fonts::pick_default_source(false, true) == "system");
    CHECK(hanabi::fonts::pick_default_source(false, false) == "bundled");
}

int main() {
    test_every_alias_gets_its_own_real_face();
    test_a_missing_weight_falls_to_the_nearest_real_face_and_says_so();
    test_emphasis_off_is_a_choice_not_a_fallback();
    test_the_emphasis_alias_draws_the_chosen_weight();
    test_the_default_family_is_the_reference_face_then_the_platform_then_bundled();
    if (failures == 0) {
        std::puts("font weights: PASS");
        return 0;
    }
    std::printf("font weights: %d failure(s)\n", failures);
    return 1;
}
