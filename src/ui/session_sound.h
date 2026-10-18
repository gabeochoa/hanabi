#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hanabi::session_sound {

enum class Tone : int { Inherit = 0, Silent = -1, TwoTone = 1, Low = 2, Bright = 3, Triple = 4 };

inline constexpr std::array<Tone, 6> kMenuOrder{Tone::Inherit, Tone::TwoTone, Tone::Low,
                                                Tone::Bright, Tone::Triple, Tone::Silent};

inline constexpr std::array<Tone, 4> kAudible{Tone::TwoTone, Tone::Low, Tone::Bright, Tone::Triple};

inline Tone from_int(int v) {
    switch (v) {
        case -1: return Tone::Silent;
        case 1: return Tone::TwoTone;
        case 2: return Tone::Low;
        case 3: return Tone::Bright;
        case 4: return Tone::Triple;
        default: return v > 0 ? Tone::TwoTone : Tone::Inherit;
    }
}

inline int to_int(Tone t) { return static_cast<int>(t); }

inline std::string_view wire(Tone t) {
    switch (t) {
        case Tone::Silent: return "silent";
        case Tone::TwoTone: return "two-tone";
        case Tone::Low: return "low";
        case Tone::Bright: return "bright";
        case Tone::Triple: return "triple";
        case Tone::Inherit: break;
    }
    return "inherit";
}

inline std::optional<Tone> parse(std::string_view s) {
    for (Tone t : kMenuOrder)
        if (wire(t) == s) return t;
    if (s == "on") return Tone::TwoTone;
    if (s == "off") return Tone::Silent;
    if (s == "default") return Tone::Inherit;
    return std::nullopt;
}

inline std::string_view label(Tone t) {
    switch (t) {
        case Tone::Silent: return "Silent";
        case Tone::TwoTone: return "Two-tone";
        case Tone::Low: return "Low";
        case Tone::Bright: return "Bright";
        case Tone::Triple: return "Triple";
        case Tone::Inherit: break;
    }
    return "Default";
}

inline std::string menu_label(Tone t, bool globalSoundOn) {
    std::string out = "Sound: ";
    out += label(t);
    if (t == Tone::Inherit) out += globalSoundOn ? " (on)" : " (off)";
    return out;
}

inline std::string action_id(Tone t) { return "sound:" + std::string(wire(t)); }

inline std::string debug_name(Tone t) {
    switch (t) {
        case Tone::Silent: return "row_menu_sound_off";
        case Tone::TwoTone: return "row_menu_sound_two_tone";
        case Tone::Low: return "row_menu_sound_low";
        case Tone::Bright: return "row_menu_sound_bright";
        case Tone::Triple: return "row_menu_sound_triple";
        case Tone::Inherit: break;
    }
    return "row_menu_sound_default";
}

struct Note {
    double hz = 0.0;
    double seconds = 0.0;
};

struct Sequence {
    std::array<Note, 3> notes{};
    std::size_t count = 0;
};

inline Sequence notes(Tone t) {
    switch (t) {
        case Tone::TwoTone: return {{Note{880.0, 0.12}, Note{1174.66, 0.18}}, 2};
        case Tone::Low: return {{Note{329.63, 0.14}, Note{440.0, 0.20}}, 2};
        case Tone::Bright: return {{Note{1318.51, 0.10}, Note{1760.0, 0.16}}, 2};
        case Tone::Triple: return {{Note{659.25, 0.10}, Note{880.0, 0.10}, Note{1174.66, 0.16}}, 3};
        case Tone::Inherit:
        case Tone::Silent: break;
    }
    return {};
}

inline constexpr Tone kDefaultChime = Tone::TwoTone;

struct Voice {
    std::optional<Tone> chime;
    bool banner_sound = false;
};

inline Voice resolve(Tone override_, bool globalChimeOn, bool globalBannerSoundOn) {
    Voice v;
    switch (override_) {
        case Tone::Silent: return v;
        case Tone::Inherit:
            if (globalChimeOn) v.chime = kDefaultChime;
            v.banner_sound = globalBannerSoundOn;
            return v;
        default:
            v.chime = override_;
            v.banner_sound = true;
            return v;
    }
}

}
