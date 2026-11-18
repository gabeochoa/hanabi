#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::background {

enum class Preset : int { Off = 0, Gradient, Glow, Aurora, Lava, Embers, Solid, Photo };

struct Entry {
    Preset preset;
    const char* key;
    const char* label;
};

inline constexpr Entry kPresets[] = {
    {Preset::Off, "off", "Off"},
    {Preset::Gradient, "gradient", "Moving Gradient"},
    {Preset::Glow, "glow", "Mouse Glow"},
    {Preset::Aurora, "aurora", "Aurora Veil"},
    {Preset::Lava, "lava", "Lava Lamp"},
    {Preset::Embers, "embers", "Ember Motes"},
    {Preset::Solid, "solid", "Solid Colour"},
    {Preset::Photo, "photo", "Photo"},
};
inline constexpr std::size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

inline Preset preset_from_key(std::string_view key) {
    for (const Entry& e : kPresets)
        if (key == e.key) return e.preset;
    return Preset::Off;
}
inline const char* key_of(Preset p) {
    for (const Entry& e : kPresets)
        if (e.preset == p) return e.key;
    return "off";
}
inline const char* label_of(Preset p) {
    for (const Entry& e : kPresets)
        if (e.preset == p) return e.label;
    return "Off";
}

enum class Dim : int { Subtle = 0, Balanced, Vivid };
inline constexpr const char* kDimKeys[] = {"subtle", "balanced", "vivid"};
inline constexpr const char* kDimLabels[] = {"Subtle", "Balanced", "Vivid"};
inline Dim dim_from_key(std::string_view key) {
    for (int i = 0; i < 3; ++i)
        if (key == kDimKeys[i]) return static_cast<Dim>(i);
    return Dim::Balanced;
}
inline const char* key_of(Dim d) { return kDimKeys[static_cast<int>(d)]; }
inline float scrim_alpha(Dim d) {
    switch (d) {
        case Dim::Subtle: return 0.80f;
        case Dim::Vivid: return 0.34f;
        case Dim::Balanced: break;
    }
    return 0.58f;
}

struct Rgba {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

// The solid-colour store (kt-nede, the reference's b405506ecc9d: a background
// is a photo or a solid colour, kept in one store beside the animated
// presets). Colours are named, not free hex, so the Settings swatches and the
// persisted value can never disagree about what a key means.
struct ColourEntry {
    const char* key;
    const char* label;
    const char* hex;  // #RRGGBB
};
inline constexpr ColourEntry kColours[] = {
    {"midnight", "Midnight", "#16233F"}, {"forest", "Forest", "#1E3D2F"},
    {"plum", "Plum", "#3A2340"},         {"ember", "Ember", "#4A2A1A"},
    {"slate", "Slate", "#2E3440"},       {"paper", "Paper", "#E8E2D6"},
};
inline constexpr std::size_t kColourCount = sizeof(kColours) / sizeof(kColours[0]);

inline Rgba rgba_from_hex(std::string_view hex, Rgba fallback) {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    if (hex.size() != 6) return fallback;
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const auto byte = [&](std::size_t i) -> float {
        const int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        return (hi < 0 || lo < 0) ? -1.0f : static_cast<float>(hi * 16 + lo) / 255.0f;
    };
    const float r = byte(0), g = byte(2), b = byte(4);
    if (r < 0.0f || g < 0.0f || b < 0.0f) return fallback;
    return {r, g, b, 1.0f};
}
inline const ColourEntry& colour_from_key(std::string_view key) {
    for (const ColourEntry& e : kColours)
        if (key == e.key) return e;
    return kColours[0];
}
inline Rgba solid_rgba(std::string_view colourKey) {
    return rgba_from_hex(colour_from_key(colourKey).hex, {0.09f, 0.14f, 0.25f, 1.0f});
}

struct Uniforms {
    float resolution[2] = {1.0f, 1.0f};
    float time = 0.0f;
    float time_delta = 0.0f;
    int frame = 0;
    float mouse[2] = {0.0f, 0.0f};  // in pane coordinates
    bool mouse_inside = false;
    float scroll = 0.0f;            // transcript scroll offset, pixels
    float pane_origin[2] = {0.0f, 0.0f};
    bool focus = true;
    float time_focus = 0.0f;
    bool reduced_motion = false;
    int status = 0;                 // 0 idle, 1 running, 2 waiting on you
    float time_status = 0.0f;
    float stream_rate = 0.0f;       // rows per second landing live
    float run_elapsed = 0.0f;
    bool tool_active = false;
    Rgba background;
    Rgba foreground;
    Rgba accent;
    bool light = false;
    std::uint32_t thread_hash = 0;
    float glow = 0.0f;              // 0..1, the pane's attention pulse
    Rgba solid{0.09f, 0.14f, 0.25f, 1.0f};  // Preset::Solid's colour
    std::string photo;                      // Preset::Photo's file ("" = none)
};

struct Quad {
    float x = 0, y = 0, w = 0, h = 0;
    Rgba tl, bl, tr, br;
};
struct Blob {
    float x = 0, y = 0, r = 0;
    Rgba inner, outer;
};
struct Scene {
    std::vector<Quad> quads;
    std::vector<Blob> blobs;
    bool additive = false;
    std::string photo;  // Preset::Photo: drawn (fitted, centred) under the scrim
    [[nodiscard]] bool empty() const { return quads.empty() && blobs.empty() && photo.empty(); }
};

inline std::uint32_t hash_id(std::string_view id) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : id) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

namespace detail {
constexpr float kTau = 6.28318530718f;
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline Rgba mix(Rgba a, Rgba b, float t) {
    t = clamp01(t);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}
inline Rgba with_alpha(Rgba c, float a) { return {c.r, c.g, c.b, clamp01(a)}; }
inline float lane(std::uint32_t hash, int k) {
    std::uint32_t v = hash ^ (static_cast<std::uint32_t>(k) * 0x9E3779B9u);
    v ^= v >> 15;
    v *= 0x2C1B3C6Du;
    v ^= v >> 12;
    return static_cast<float>(v & 0xFFFFu) / 65536.0f;
}
inline float clock(const Uniforms& u) {
    if (u.reduced_motion) return 0.0f;
    if (!u.focus) return u.time_focus;
    return u.time;
}
inline Rgba warm(const Uniforms& u) {
    const Rgba amber{1.0f, 0.62f, 0.25f, 1.0f};
    return mix(u.accent, amber, u.light ? 0.25f : 0.45f);
}
inline Rgba cool(const Uniforms& u) {
    const Rgba teal{0.25f, 0.75f, 0.85f, 1.0f};
    return mix(u.accent, teal, 0.5f);
}
}  // namespace detail

inline void gradient(const Uniforms& u, Scene& out) {
    using namespace detail;
    const float t = clock(u) * 0.06f + lane(u.thread_hash, 1);
    const float w = u.resolution[0], h = u.resolution[1];
    const int bands = 3;
    const float bw = w / static_cast<float>(bands);
    const float strength = u.light ? 0.28f : 0.38f;
    const auto top = [&](int i) {
        const float ph = t * kTau + static_cast<float>(i) * 2.1f;
        return with_alpha(mix(u.accent, cool(u), 0.5f + 0.5f * std::sin(ph)), strength);
    };
    const auto bottom = [&](int i) {
        const float ph = t * kTau + static_cast<float>(i) * 2.1f;
        return with_alpha(mix(u.accent, warm(u), 0.5f + 0.5f * std::cos(ph * 0.7f)),
                          strength * 0.8f);
    };
    for (int i = 0; i < bands; ++i)
        out.quads.push_back(Quad{static_cast<float>(i) * bw, 0.0f, bw + 1.0f, h, top(i), bottom(i),
                                 top(i + 1), bottom(i + 1)});
}

inline void glow(const Uniforms& u, Scene& out) {
    using namespace detail;
    const float w = u.resolution[0], h = u.resolution[1];
    const float cx = u.mouse_inside ? u.mouse[0] : w * 0.5f;
    const float cy = u.mouse_inside ? u.mouse[1] : h * 0.42f;
    const float base = std::min(w, h) * 0.48f + u.glow * 60.0f;
    const float pulse = u.status == 1 ? 1.0f + 0.06f * std::sin(clock(u) * 2.2f) : 1.0f;
    const float alpha = (u.light ? 0.22f : 0.34f) + u.glow * 0.15f;
    out.blobs.push_back(Blob{cx, cy, base * pulse, with_alpha(u.accent, alpha),
                             with_alpha(u.accent, 0.0f)});
    out.blobs.push_back(Blob{cx, cy, base * 0.45f * pulse, with_alpha(warm(u), alpha * 0.5f),
                             with_alpha(warm(u), 0.0f)});
    out.additive = !u.light;
}

inline void aurora(const Uniforms& u, Scene& out) {
    using namespace detail;
    const float w = u.resolution[0], h = u.resolution[1];
    const float t = clock(u);
    const float r = std::max(w, h) * 0.42f;
    for (int k = 0; k < 5; ++k) {
        const float p = lane(u.thread_hash, 10 + k) * kTau;
        const float x = w * (0.1f + 0.8f * static_cast<float>(k) / 4.0f) +
                        std::sin(t * 0.11f + p) * w * 0.09f;
        const float y = h * 0.30f + std::cos(t * 0.07f + p * 1.3f) * h * 0.14f;
        const Rgba c = (k % 2 == 0) ? cool(u) : mix(u.accent, u.foreground, 0.25f);
        const float a = (u.light ? 0.09f : 0.13f) + 0.03f * std::sin(t * 0.23f + p);
        out.blobs.push_back(Blob{x, y, r, with_alpha(c, a), with_alpha(c, 0.0f)});
    }
    out.additive = !u.light;
}

inline void lava(const Uniforms& u, Scene& out) {
    using namespace detail;
    const float w = u.resolution[0], h = u.resolution[1];
    const float t = clock(u);
    const float r = std::min(w, h) * 0.24f;
    for (int k = 0; k < 6; ++k) {
        const float p = lane(u.thread_hash, 20 + k) * kTau;
        const float q = lane(u.thread_hash, 30 + k);
        const float x = w * (0.5f + 0.38f * std::sin(t * (0.05f + 0.02f * q) + p));
        const float y = h * (0.5f + 0.36f * std::cos(t * (0.04f + 0.015f * q) + p * 0.7f));
        const Rgba c = (k % 3 == 0) ? warm(u) : (k % 3 == 1) ? u.accent : cool(u);
        const float a = u.light ? 0.16f : 0.26f;
        out.blobs.push_back(Blob{x, y, r * (0.8f + 0.4f * q), with_alpha(c, a), with_alpha(c, 0.0f)});
    }
    out.additive = !u.light;
}

inline void embers(const Uniforms& u, Scene& out) {
    using namespace detail;
    const float w = u.resolution[0], h = u.resolution[1];
    const float t = clock(u);
    const float speed = 18.0f + std::min(u.stream_rate, 6.0f) * 6.0f;
    const Rgba ember = warm(u);
    for (int k = 0; k < 24; ++k) {
        const float px = lane(u.thread_hash, 40 + k);
        const float py = lane(u.thread_hash, 70 + k);
        const float ps = 0.6f + 0.8f * lane(u.thread_hash, 100 + k);
        const float travel = h + 40.0f;
        const float yy = std::fmod(py * travel + t * speed * ps, travel);
        const float y = h + 20.0f - yy;
        const float x = px * w + std::sin(t * 0.6f + px * kTau) * 14.0f;
        const float life = 1.0f - yy / travel;  // 1 at the foot, 0 at the top
        const float a = (u.light ? 0.30f : 0.45f) * life * (u.tool_active ? 1.3f : 1.0f);
        out.blobs.push_back(Blob{x, y, 6.0f + 8.0f * ps, with_alpha(ember, a), with_alpha(ember, 0.0f)});
    }
    out.additive = !u.light;
}

inline Scene scene_for(Preset p, const Uniforms& u) {
    Scene out;
    if (u.resolution[0] <= 0.0f || u.resolution[1] <= 0.0f) return out;
    switch (p) {
        case Preset::Off: break;
        case Preset::Gradient: gradient(u, out); break;
        case Preset::Glow: glow(u, out); break;
        case Preset::Aurora: aurora(u, out); break;
        case Preset::Lava: lava(u, out); break;
        case Preset::Embers: embers(u, out); break;
        case Preset::Solid:
            out.quads.push_back(Quad{0.0f, 0.0f, u.resolution[0], u.resolution[1], u.solid,
                                     u.solid, u.solid, u.solid});
            break;
        case Preset::Photo: out.photo = u.photo; break;
    }
    return out;
}

inline std::size_t primitive_count(const Scene& s) { return s.quads.size() + s.blobs.size(); }

}  // namespace hanabi::background
