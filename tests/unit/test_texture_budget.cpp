// What the texture cache's budget actually bounds.
//
// Every claim here used to be a paragraph of comment in src/ui/inline_image.h,
// in a header that cannot be compiled without a GPU, so none of them could be
// checked. The policy now lives in src/util/texture_budget.h with no texture
// in it, and this drives it with synthetic entries sized by the same
// arithmetic the app uses.
//
// The headline case is the first one. A 32 MB budget counted in `w*h*4` held
// 27 images and 47 MB of GPU memory, because afterhours uploads a mip chain on
// top of every base level (util/gpu_mem.h). Counted properly it holds 20 and
// 32 MB. The budget did not change; what changed is that it is now true.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/util/gpu_mem.h"
#include "../../src/util/texture_budget.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::texbudget::Budget;

static std::string key(int i) { return "/img/" + std::to_string(i) + ".png"; }

// --- the budget is a GPU-byte budget ---------------------------------------
static void test_the_budget_bounds_what_the_device_holds() {
    std::printf("test_the_budget_bounds_what_the_device_holds\n");

    const std::size_t perImage = hanabi::gpu::texture_bytes(640, 480);
    // No protection window, so this measures the budget alone. The window has
    // its own case below and would otherwise mask this one.
    Budget b(hanabi::texbudget::kDefaultMaxBytes, 0,
             hanabi::texbudget::kDefaultMaxEntries);

    for (int i = 0; i < 60; ++i) {
        b.insert(key(i), perImage);
        b.trim([](const std::string&, std::size_t) {});
    }

    CHECK(b.bytes() <= hanabi::texbudget::kDefaultMaxBytes);
    // 32 MiB / 1,638,352 = 20. Under the old w*h*4 accounting the same budget
    // admitted 27 of these and the device held 47 MB against a 32 MB claim.
    CHECK(b.size() == 20u);
    CHECK(b.size() * perImage <= hanabi::texbudget::kDefaultMaxBytes);
    CHECK((b.size() + 1) * perImage > hanabi::texbudget::kDefaultMaxBytes);
    CHECK(b.evictions() == 40u);

    // The survivors are the most recent, and the oldest are gone.
    CHECK(b.holds(key(59)));
    CHECK(!b.holds(key(0)));

    // A Retina screen grab is 31 MB resident on its own, so two of them do not
    // fit in the budget and one does. That is the shape the bound exists for.
    const std::size_t retina = hanabi::gpu::texture_bytes(3024, 1964);
    CHECK(retina < hanabi::texbudget::kDefaultMaxBytes);
    CHECK(2 * retina > hanabi::texbudget::kDefaultMaxBytes);
}

// --- the protection window matters more than the budget ---------------------
static void test_a_frames_working_set_is_never_evicted() {
    std::printf("test_a_frames_working_set_is_never_evicted\n");

    // Sixteen Retina screen grabs is 500 MB of working set against a 32 MB
    // budget. Every one of them is inside the protection window, so the cache
    // keeps all of them: dropping one would mean re-decoding a PNG on the very
    // next frame that draws it, which costs far more than the memory saves.
    const std::size_t retina = hanabi::gpu::texture_bytes(3024, 1964);
    Budget b(hanabi::texbudget::kDefaultMaxBytes,
             hanabi::texbudget::kDefaultProtectRecent,
             hanabi::texbudget::kDefaultMaxEntries);

    std::size_t evicted = 0;
    for (std::size_t i = 0; i < hanabi::texbudget::kDefaultProtectRecent; ++i) {
        b.insert(key(static_cast<int>(i)), retina);
        evicted += b.trim([](const std::string&, std::size_t) {});
    }
    CHECK(evicted == 0u);
    CHECK(b.size() == hanabi::texbudget::kDefaultProtectRecent);
    CHECK(b.bytes() > hanabi::texbudget::kDefaultMaxBytes);
    CHECK(b.evictions() == 0u);

    // Walk past the window and the budget reasserts itself: the oldest fall
    // out as soon as they stop being recent.
    for (int i = 100; i < 132; ++i) {
        b.insert(key(i), retina);
        b.trim([](const std::string&, std::size_t) {});
    }
    CHECK(b.evictions() > 0u);
    CHECK(b.size() == hanabi::texbudget::kDefaultProtectRecent);
    CHECK(!b.holds(key(0)));
}

// --- least recently ACCESSED, not least recently inserted -------------------
static void test_eviction_is_by_access_not_by_insertion() {
    std::printf("test_eviction_is_by_access_not_by_insertion\n");

    Budget b(1000, 2, 100);
    b.insert("old-but-drawn-every-frame", 400);
    b.insert("newer-but-never-looked-at", 400);
    // Four accesses of the first, which also walks the second out of the
    // protection window. An insertion-ordered cache would drop the first.
    for (int i = 0; i < 4; ++i) b.touch("old-but-drawn-every-frame");

    std::vector<std::string> gone;
    b.insert("the-new-one", 400);
    b.trim([&](const std::string& k, std::size_t) { gone.push_back(k); });

    CHECK(gone.size() == 1u);
    CHECK(gone.size() == 1u && gone[0] == "newer-but-never-looked-at");
    CHECK(b.holds("old-but-drawn-every-frame"));
    CHECK(b.holds("the-new-one"));
}

// --- a failed load costs no bytes, so only the entry cap can bound it -------
static void test_zero_byte_entries_are_bounded_by_the_entry_cap() {
    std::printf("test_zero_byte_entries_are_bounded_by_the_entry_cap\n");

    // Ten thousand bad paths. Not one of them costs a GPU byte, so the byte
    // budget never fires and the map would grow forever on the entry side.
    Budget b(hanabi::texbudget::kDefaultMaxBytes, 0,
             hanabi::texbudget::kDefaultMaxEntries);
    for (int i = 0; i < 10000; ++i) {
        b.insert(key(i), 0);
        b.trim([](const std::string&, std::size_t) {});
    }
    CHECK(b.bytes() == 0u);
    CHECK(b.size() == hanabi::texbudget::kDefaultMaxEntries);
    CHECK(b.holds(key(9999)));
}

// --- the same key twice is one entry, not two ------------------------------
static void test_reinserting_a_key_does_not_double_count_it() {
    std::printf("test_reinserting_a_key_does_not_double_count_it\n");

    Budget b(hanabi::texbudget::kDefaultMaxBytes, 0,
             hanabi::texbudget::kDefaultMaxEntries);
    b.insert("a.png", 1000);
    b.insert("a.png", 1000);
    CHECK(b.bytes() == 1000u);
    CHECK(b.size() == 1u);

    // Re-inserting at a different size replaces the old figure rather than
    // adding to it -- which is what a reload at a new display size does.
    b.insert("a.png", 250);
    CHECK(b.bytes() == 250u);
    CHECK(b.bytes_of("a.png") == 250u);
}

// --- churn is visible, because a byte total cannot show it ------------------
static void test_thrashing_reads_as_flat_bytes_and_climbing_inserts() {
    std::printf("test_thrashing_reads_as_flat_bytes_and_climbing_inserts\n");

    // Two entries that do not both fit, alternating, with no protection. The
    // byte total is identical on every iteration; only the counters show that
    // the cache is doing nothing but reload.
    Budget b(1000, 0, 100);
    for (int i = 0; i < 50; ++i) {
        b.insert(i % 2 == 0 ? "a.png" : "b.png", 900);
        b.trim([](const std::string&, std::size_t) {});
    }
    CHECK(b.bytes() == 900u);
    CHECK(b.size() == 1u);
    CHECK(b.inserts() == 50u);
    CHECK(b.evictions() == 49u);
}

// --- the OTHER bound: sokol's object pools, which bytes cannot express ------
static void test_the_entry_cap_fits_inside_sokols_sampler_pool() {
    std::printf("test_the_entry_cap_fits_inside_sokols_sampler_pool\n");

    // afterhours makes one image, one view AND one sampler per texture, and
    // sokol's sampler pool is 64 against the image pool's 128 -- so samplers
    // are the binding constraint. Measured in a process that had done exactly
    // what hanabi's launch does: the sampler pool ran out after 61 loads.
    //
    // Past that point load_texture_from_pixels returns a texture with a valid
    // image, a valid view, sampler_id == 0 and the file's real dimensions,
    // which every "did it load?" test in this app reads as success. So the
    // entry cap is not a nicety, it is the only thing between the cache and
    // textures that silently do not draw.
    CHECK(hanabi::gpu::kMaxLiveTextures < hanabi::gpu::kSokolSamplerPool);
    CHECK(hanabi::gpu::kMaxLiveTextures < hanabi::gpu::kSokolImagePool);
    CHECK(hanabi::texbudget::kDefaultMaxEntries <=
          hanabi::gpu::kMaxLiveTextures);
    // And it still holds more than a frame can draw at once, or the protection
    // window would be evicting inside a frame.
    CHECK(hanabi::texbudget::kDefaultMaxEntries >
          hanabi::texbudget::kDefaultProtectRecent);
}

static void test_many_small_images_stop_at_the_entry_cap() {
    std::printf("test_many_small_images_stop_at_the_entry_cap\n");

    // A 96x96 avatar is 49 KB resident, so a 32 MB byte budget is SIX HUNDRED
    // of them and never fires. Eighty distinct ones -- one board's worth of
    // per-thread avatars -- is already past the sampler pool.
    const std::size_t tiny = hanabi::gpu::texture_bytes(96, 96);
    CHECK(tiny * 600 < hanabi::texbudget::kDefaultMaxBytes);

    Budget b(hanabi::texbudget::kDefaultMaxBytes, 0,
             hanabi::texbudget::kDefaultMaxEntries);
    for (int i = 0; i < 80; ++i) {
        b.insert(key(i), tiny);
        b.trim([](const std::string&, std::size_t) {});
    }
    // The byte budget did nothing here -- it never came near firing.
    CHECK(b.bytes() < hanabi::texbudget::kDefaultMaxBytes);
    CHECK(b.size() == hanabi::texbudget::kDefaultMaxEntries);
    CHECK(b.size() <= hanabi::gpu::kMaxLiveTextures);
    CHECK(b.evictions() == 80u - hanabi::texbudget::kDefaultMaxEntries);
}

int main() {
    std::printf("=== test_texture_budget ===\n");
    test_the_entry_cap_fits_inside_sokols_sampler_pool();
    test_many_small_images_stop_at_the_entry_cap();
    test_the_budget_bounds_what_the_device_holds();
    test_a_frames_working_set_is_never_evicted();
    test_eviction_is_by_access_not_by_insertion();
    test_zero_byte_entries_are_bounded_by_the_entry_cap();
    test_reinserting_a_key_does_not_double_count_it();
    test_thrashing_reads_as_flat_bytes_and_climbing_inserts();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
