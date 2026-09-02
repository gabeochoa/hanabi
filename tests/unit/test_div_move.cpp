#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include <afterhours/tests/ui_test_harness.h>

#include "../../src/ui/div.h"
#include "../../src/ui/mk.h"

static int g_failures = 0;
#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

static unsigned long long g_allocs = 0;
static bool g_counting = false;

void* operator new(std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using afterhours::ui::ComponentSize;
using afterhours::ui::pixels;
using afterhours::ui::imm::ComponentConfig;

static const char* kRichSub = "ready for review  \xc2\xb7  1d";
static const char* kSparseSub = "3h";

static constexpr int kWarm = 8;
static constexpr int kReps = 200;

static unsigned long long through_library(const std::string& label) {
    ui_test::ImmTestHarness h;
    auto& ctx = h.context();
    auto& root = h.root();
    for (int i = 0; i < kWarm; ++i)
        afterhours::ui::imm::div(ctx, hanabi::ui::mk(root, 1),
                                 ComponentConfig{}
                                     .with_label(label)
                                     .with_size(ComponentSize{pixels(320),
                                                              pixels(16)})
                                     .with_transparent_bg()
                                     .with_roundness(0.0f)
                                     .with_debug_name("dc_sub"));
    g_allocs = 0;
    g_counting = true;
    for (int i = 0; i < kReps; ++i)
        afterhours::ui::imm::div(ctx, hanabi::ui::mk(root, 1),
                                 ComponentConfig{}
                                     .with_label(label)
                                     .with_size(ComponentSize{pixels(320),
                                                              pixels(16)})
                                     .with_transparent_bg()
                                     .with_roundness(0.0f)
                                     .with_debug_name("dc_sub"));
    g_counting = false;
    return g_allocs;
}

static unsigned long long through_hanabi(const std::string& label) {
    ui_test::ImmTestHarness h;
    auto& ctx = h.context();
    auto& root = h.root();
    for (int i = 0; i < kWarm; ++i)
        hanabi::ui::div(ctx, hanabi::ui::mk(root, 1),
                        ComponentConfig{}
                            .with_label(label)
                            .with_size(ComponentSize{pixels(320), pixels(16)})
                            .with_transparent_bg()
                            .with_roundness(0.0f)
                            .with_debug_name("dc_sub"));
    g_allocs = 0;
    g_counting = true;
    for (int i = 0; i < kReps; ++i)
        hanabi::ui::div(ctx, hanabi::ui::mk(root, 1),
                        ComponentConfig{}
                            .with_label(label)
                            .with_size(ComponentSize{pixels(320), pixels(16)})
                            .with_transparent_bg()
                            .with_roundness(0.0f)
                            .with_debug_name("dc_sub"));
    g_counting = false;
    return g_allocs;
}

static void heap_label_saves_exactly_one_copy_per_widget() {
    std::printf("heap_label_saves_exactly_one_copy_per_widget\n");
    const std::string label(kRichSub);
    REQUIRE(label.size() > 22);

    const unsigned long long lib = through_library(label);
    const unsigned long long han = through_hanabi(label);

    std::printf("  afterhours::ui::imm::div : %llu allocations / %d widgets\n",
                lib, kReps);
    std::printf("  hanabi::ui::div          : %llu allocations / %d widgets\n",
                han, kReps);

    REQUIRE(lib >= static_cast<unsigned long long>(kReps));
    REQUIRE(han + static_cast<unsigned long long>(kReps) == lib);
}

static void sso_label_saves_nothing() {
    std::printf("sso_label_saves_nothing\n");
    const std::string label(kSparseSub);
    REQUIRE(label.size() <= 22);

    const unsigned long long lib = through_library(label);
    const unsigned long long han = through_hanabi(label);

    std::printf("  afterhours::ui::imm::div : %llu allocations / %d widgets\n",
                lib, kReps);
    std::printf("  hanabi::ui::div          : %llu allocations / %d widgets\n",
                han, kReps);

    REQUIRE(han == lib);
}

static void moved_config_still_delivers_the_whole_label() {
    std::printf("moved_config_still_delivers_the_whole_label\n");
    const std::string label(kRichSub);

    ui_test::ImmTestHarness h;
    auto& ctx = h.context();
    auto& root = h.root();

    auto el = hanabi::ui::div(
        ctx, hanabi::ui::mk(root, 3),
        ComponentConfig{}.with_label(label).with_size(
            ComponentSize{pixels(320), pixels(16)}));
    REQUIRE(el.ent().has<afterhours::ui::HasLabel>());
    REQUIRE(el.ent().get<afterhours::ui::HasLabel>().label == label);

    auto again = hanabi::ui::div(
        ctx, hanabi::ui::mk(root, 3),
        ComponentConfig{}.with_label("3h").with_size(
            ComponentSize{pixels(320), pixels(16)}));
    REQUIRE(again.ent().get<afterhours::ui::HasLabel>().label == "3h");
}

static void a_named_config_is_delivered_intact() {
    std::printf("a_named_config_is_delivered_intact\n");
    const std::string label(kRichSub);

    ui_test::ImmTestHarness h;
    auto& ctx = h.context();
    auto& root = h.root();

    auto cfg = ComponentConfig{}.with_label(label).with_size(
        ComponentSize{pixels(320), pixels(16)});
    cfg.with_debug_name("named_cfg");
    auto el = hanabi::ui::div(ctx, hanabi::ui::mk(root, 4), cfg);
    REQUIRE(el.ent().get<afterhours::ui::HasLabel>().label == label);
}

static void a_named_config_is_consumed_by_div() {
    std::printf("a_named_config_is_consumed_by_div\n");
    const std::string label(kRichSub);
    REQUIRE(label.size() > 22);

    ui_test::ImmTestHarness h;
    auto& ctx = h.context();
    auto& root = h.root();

    auto cfg = ComponentConfig{}.with_label(label).with_size(
        ComponentSize{pixels(320), pixels(16)});

    auto first = hanabi::ui::div(ctx, hanabi::ui::mk(root, 5), cfg);
    REQUIRE(first.ent().has<afterhours::ui::HasLabel>());
    REQUIRE(first.ent().get<afterhours::ui::HasLabel>().label == label);

    REQUIRE(cfg.label.empty());

    auto second = hanabi::ui::div(ctx, hanabi::ui::mk(root, 6), cfg);
    const bool second_carries_the_label =
        second.ent().has<afterhours::ui::HasLabel>() &&
        second.ent().get<afterhours::ui::HasLabel>().label == label;
    REQUIRE(!second_carries_the_label);
}

int main() {
    std::printf("=== test_div_move ===\n");
    heap_label_saves_exactly_one_copy_per_widget();
    sso_label_saves_nothing();
    moved_config_still_delivers_the_whole_label();
    a_named_config_is_delivered_intact();
    a_named_config_is_consumed_by_div();
    if (g_failures == 0) {
        std::printf("test_div_move: PASS\n");
        return 0;
    }
    std::printf("test_div_move: %d FAILURE(S)\n", g_failures);
    return 1;
}
