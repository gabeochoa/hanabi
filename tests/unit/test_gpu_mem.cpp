// What a texture actually costs, and whether the app's number for it is the
// same number the GPU is holding.
//
// The claim under test is one line of arithmetic, and it was wrong everywhere
// in the app before this: `w * h * 4`. afterhours' load_texture builds and
// uploads a full box-filtered MIP CHAIN at image creation (sokol has no
// runtime mipmap generation, so the levels have to be supplied up front), so
// every texture is bigger than its base level by the chain -- about a third
// again. A cache budgeting against w*h*4 therefore believes it is holding
// 32 MB while the device holds 43.
//
// The reference below deliberately does NOT share the implementation's loop.
// It materialises the level list and sums it, which is a different shape of
// mistake to make, so the two agreeing is evidence rather than a tautology.

#include <cstdio>
#include <utility>
#include <vector>

#include "../../src/util/gpu_mem.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// Every mip level afterhours uploads, base first. Mirrors build_mip_chain's
// termination (`while (pw > 1 || ph > 1)`) and its per-axis floor at 1, which
// is the part a naive implementation gets wrong: a 1024x1 strip still has ten
// levels, because only ONE axis has run out.
static std::vector<std::pair<int, int>> levels(int w, int h) {
    std::vector<std::pair<int, int>> out;
    out.emplace_back(w, h);
    while (out.back().first > 1 || out.back().second > 1) {
        const auto [pw, ph] = out.back();
        out.emplace_back(pw > 1 ? pw / 2 : 1, ph > 1 ? ph / 2 : 1);
    }
    return out;
}

static std::size_t reference_bytes(int w, int h) {
    std::size_t total = 0;
    for (const auto& [lw, lh] : levels(w, h))
        total += static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh) * 4u;
    return total;
}

static void test_a_texture_costs_more_than_its_base_level() {
    std::printf("test_a_texture_costs_more_than_its_base_level\n");

    // The two shapes this app actually loads: the 640x480 fixture the memory
    // ladder attaches, and a Retina screen grab, which is what a person
    // pastes.
    const std::size_t small = hanabi::gpu::texture_bytes(640, 480);
    CHECK(small == reference_bytes(640, 480));
    CHECK(small == 1638352u);
    CHECK(small > 640u * 480u * 4u);

    const std::size_t retina = hanabi::gpu::texture_bytes(3024, 1964);
    CHECK(retina == reference_bytes(3024, 1964));
    CHECK(retina > 3024u * 1964u * 4u);

    // The chain adds almost exactly a third on top of the base for a
    // reasonably square texture: each level is a quarter of the one above, so
    // the sum converges on base/3 from BELOW and never reaches it. Both
    // bounds are asserted because the interesting failure is one-sided -- a
    // `w*h*4` estimate is not conservative in the direction that matters. It
    // UNDER-reports what the device holds, so a budget built on it is quietly
    // a third larger than it says it is.
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{
             {64, 64}, {128, 96}, {640, 480}, {1920, 1080}, {3024, 1964}}) {
        const std::size_t base =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        const std::size_t all = hanabi::gpu::texture_bytes(w, h);
        CHECK(all > base);
        CHECK((all - base) * 100u >= base * 33u);
        CHECK((all - base) * 3u <= base);
    }
}

static void test_a_degenerate_strip_still_has_a_chain() {
    std::printf("test_a_degenerate_strip_still_has_a_chain\n");

    // 1024x1: one axis is already at the floor. The chain keeps halving the
    // other, so this is eleven levels, not one -- the case a
    // `while (w > 1 && h > 1)` loop silently drops.
    CHECK(hanabi::gpu::texture_bytes(1024, 1) == reference_bytes(1024, 1));
    CHECK(hanabi::gpu::texture_bytes(1024, 1) > 1024u * 4u);
    CHECK(levels(1024, 1).size() == 11u);

    CHECK(hanabi::gpu::texture_bytes(1, 1) == 4u);
    CHECK(hanabi::gpu::texture_bytes(0, 100) == 0u);
    CHECK(hanabi::gpu::texture_bytes(-4, 4) == 0u);
}

static void test_the_ledger_tracks_live_bytes_and_churn() {
    std::printf("test_the_ledger_tracks_live_bytes_and_churn\n");

    hanabi::gpu::Ledger& l = hanabi::gpu::ledger();
    l = hanabi::gpu::Ledger{};

    const std::size_t one = hanabi::gpu::texture_bytes(640, 480);
    hanabi::gpu::note_load(one);
    hanabi::gpu::note_load(one);
    CHECK(hanabi::gpu::ledger_bytes() == 2 * one);
    CHECK(hanabi::gpu::ledger_live() == 2u);

    hanabi::gpu::note_unload(one);
    CHECK(hanabi::gpu::ledger_bytes() == one);
    CHECK(hanabi::gpu::ledger_live() == 1u);

    // Churn is the number a byte counter cannot show: a cache thrashing at a
    // steady size reads as flat bytes and a climbing load count.
    CHECK(l.loads == 2u);
    CHECK(l.unloads == 1u);
    CHECK(l.peakBytes == 2 * one);

    // Unloading more than is held clamps at zero rather than wrapping. A
    // size_t that wraps here would report 18 exabytes and read as a
    // catastrophic leak, which is the worst possible failure for an
    // instrument whose whole job is to be believed.
    hanabi::gpu::note_unload(one * 10);
    CHECK(hanabi::gpu::ledger_bytes() == 0u);
    CHECK(hanabi::gpu::ledger_live() == 0u);
}

static void test_device_accounting_says_when_it_is_absent() {
    std::printf("test_device_accounting_says_when_it_is_absent\n");

    // This binary links no Metal translation unit, so the weak symbol is null
    // and the answer must be "not measured" rather than a confident zero.
    // GATES.md's own rule: a gate that reports nothing is not a gate that
    // passed.
    CHECK(!hanabi::gpu::device_accounting());
    CHECK(hanabi::gpu::device_bytes() == 0ull);
}

int main() {
    std::printf("=== test_gpu_mem ===\n");
    test_a_texture_costs_more_than_its_base_level();
    test_a_degenerate_strip_still_has_a_chain();
    test_the_ledger_tracks_live_bytes_and_churn();
    test_device_accounting_says_when_it_is_absent();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
