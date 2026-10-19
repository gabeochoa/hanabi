#include <cmath>
#include <cstdio>
#include <string>

#include "../../src/ui/background.h"

namespace bg = hanabi::background;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bg::Uniforms sample(float t) {
    bg::Uniforms u;
    u.resolution[0] = 900.0f;
    u.resolution[1] = 600.0f;
    u.time = t;
    u.mouse[0] = 300.0f;
    u.mouse[1] = 200.0f;
    u.mouse_inside = true;
    u.accent = {0.42f, 0.55f, 1.0f, 1.0f};
    u.foreground = {0.9f, 0.9f, 0.92f, 1.0f};
    u.background = {0.08f, 0.08f, 0.1f, 1.0f};
    u.thread_hash = bg::hash_id("t2x1");
    return u;
}

static bool sane(const bg::Scene& s, float w, float h) {
    const auto ok = [](const bg::Rgba& c) {
        return c.r >= 0 && c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1 && c.a >= 0 &&
               c.a <= 1;
    };
    for (const auto& q : s.quads)
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || q.w <= 0 || q.h <= 0 || !ok(q.tl) ||
            !ok(q.bl) || !ok(q.tr) || !ok(q.br))
            return false;
    for (const auto& b : s.blobs)
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || b.r <= 0 || b.x < -2 * w || b.x > 3 * w ||
            b.y < -2 * h || b.y > 3 * h || !ok(b.inner) || !ok(b.outer))
            return false;
    return true;
}

static bool same(const bg::Scene& a, const bg::Scene& b) {
    if (a.quads.size() != b.quads.size() || a.blobs.size() != b.blobs.size()) return false;
    for (std::size_t i = 0; i < a.blobs.size(); ++i)
        if (a.blobs[i].x != b.blobs[i].x || a.blobs[i].y != b.blobs[i].y || a.blobs[i].r != b.blobs[i].r)
            return false;
    for (std::size_t i = 0; i < a.quads.size(); ++i)
        if (a.quads[i].tl.r != b.quads[i].tl.r || a.quads[i].br.b != b.quads[i].br.b) return false;
    return true;
}

int main() {
    std::printf("=== test_background ===\n");
    // Keys and labels round-trip; an unknown key is Off.
    CHECK(bg::kPresetCount == 6);
    for (const auto& e : bg::kPresets) {
        CHECK(bg::preset_from_key(e.key) == e.preset);
        CHECK(std::string(bg::key_of(e.preset)) == e.key);
        CHECK(std::string(bg::label_of(e.preset)) == e.label);
    }
    CHECK(bg::preset_from_key("nebula") == bg::Preset::Off);
    CHECK(bg::preset_from_key("") == bg::Preset::Off);
    CHECK(bg::dim_from_key("vivid") == bg::Dim::Vivid);
    CHECK(bg::dim_from_key("") == bg::Dim::Balanced);
    CHECK(bg::scrim_alpha(bg::Dim::Subtle) > bg::scrim_alpha(bg::Dim::Balanced));
    CHECK(bg::scrim_alpha(bg::Dim::Balanced) > bg::scrim_alpha(bg::Dim::Vivid));
    CHECK(bg::scrim_alpha(bg::Dim::Vivid) > 0.25f);

    // The hash is stable and separates ids.
    CHECK(bg::hash_id("t2x1") == bg::hash_id("t2x1"));
    CHECK(bg::hash_id("t2x1") != bg::hash_id("t2x2"));

    // Off is empty; every other preset paints something sane, at two times.
    CHECK(bg::scene_for(bg::Preset::Off, sample(3.0f)).empty());
    for (const auto& e : bg::kPresets) {
        if (e.preset == bg::Preset::Off) continue;
        for (float t : {0.0f, 7.5f, 1000.0f}) {
            const bg::Scene s = bg::scene_for(e.preset, sample(t));
            CHECK(!s.empty());
            CHECK(sane(s, 900.0f, 600.0f));
            CHECK(bg::primitive_count(s) <= 32);
        }
    }

    // Time moves the scene; Reduce Motion and an unfocused window freeze it.
    for (auto p : {bg::Preset::Gradient, bg::Preset::Aurora, bg::Preset::Lava, bg::Preset::Embers}) {
        CHECK(!same(bg::scene_for(p, sample(0.0f)), bg::scene_for(p, sample(5.0f))));
        bg::Uniforms a = sample(0.0f), b = sample(5.0f);
        a.reduced_motion = b.reduced_motion = true;
        CHECK(same(bg::scene_for(p, a), bg::scene_for(p, b)));
        bg::Uniforms c = sample(2.0f), d = sample(9.0f);
        c.focus = d.focus = false;
        c.time_focus = d.time_focus = 2.0f;
        CHECK(same(bg::scene_for(p, c), bg::scene_for(p, d)));
    }

    // Two threads get two scenes from the same clock.
    {
        bg::Uniforms a = sample(1.0f), b = sample(1.0f);
        b.thread_hash = bg::hash_id("t9");
        CHECK(!same(bg::scene_for(bg::Preset::Lava, a), bg::scene_for(bg::Preset::Lava, b)));
    }

    // The glow follows the pointer, and sits at the pane's centre without one.
    {
        bg::Uniforms u = sample(1.0f);
        const bg::Scene at = bg::scene_for(bg::Preset::Glow, u);
        CHECK(at.blobs.size() == 2 && at.blobs[0].x == 300.0f && at.blobs[0].y == 200.0f);
        u.mouse_inside = false;
        const bg::Scene centre = bg::scene_for(bg::Preset::Glow, u);
        CHECK(centre.blobs[0].x == 450.0f);
        u.glow = 1.0f;
        CHECK(bg::scene_for(bg::Preset::Glow, u).blobs[0].r > centre.blobs[0].r);
    }

    // Embers rise faster while rows land; a light palette paints with alpha, not additively.
    {
        bg::Uniforms slow = sample(4.0f), fast = sample(4.0f);
        fast.stream_rate = 5.0f;
        CHECK(!same(bg::scene_for(bg::Preset::Embers, slow), bg::scene_for(bg::Preset::Embers, fast)));
        CHECK(bg::scene_for(bg::Preset::Embers, slow).additive);
        bg::Uniforms light = sample(4.0f);
        light.light = true;
        CHECK(!bg::scene_for(bg::Preset::Embers, light).additive);
    }

    // A pane with no size paints nothing.
    {
        bg::Uniforms u = sample(1.0f);
        u.resolution[0] = 0.0f;
        CHECK(bg::scene_for(bg::Preset::Aurora, u).empty());
    }

    if (failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
