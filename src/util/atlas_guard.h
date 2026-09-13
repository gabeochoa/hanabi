#pragma once

// ---------------------------------------------------------------------------
// A measurement that came back WRONG must not be allowed to be silent.
//
// THE CONDITION. The sokol backend rasterises into one fontstash atlas that
// never grows. When a glyph will not fit, fontstash stops advancing for it, so
// `measure_text` returns short or zero -- and measure_text is the input to
// every wrap, hug, ellipsize and virtualization spacer in this app, so a
// string that measures short is LAID OUT short and one that measures zero is
// laid out as absent.
//
// WHAT UPSTREAM NOW DOES, and what it still does not. At pin 9ff9079 the
// backend registers `fonsSetErrorCallback` and warns once when the atlas
// fills, and `AFTERHOURS_FONT_ATLAS_SIZE` sets the ceiling (afterhours_gaps.md
// upstream bdea3b9). So the CONDITION is now reported.
//
// What is still missing is per-measurement completeness (#350) and a drawn
// substitute glyph (#353): nothing says WHICH measurement was short, and a
// dropped glyph is still not drawn. A warning that the atlas filled does not
// tell a layout that the number it just used was wrong, and hanabi remembers
// measurements in memos (src/util/text_cache.h, src/util/wrap_count.h), so a
// poisoned number can be cached as well as used. This file is what refuses to
// trust the answer.
//
// WHAT IT CATCHES, HONESTLY
//
//   * ZERO. A non-blank string that measures 0 is exact: every printable glyph
//     in every font hanabi ships has a non-zero advance.
//   * NOT FINITE. A NaN width poisons a comparison rather than a layout.
//   * SHORT, via `probe()`. A partial drop cannot be recognised from the
//     number alone, so probe() asks directly: it measures a glyph the atlas
//     has never held at a size it has never held, and a zero advance from THAT
//     means the atlas can take no new rect. That is the condition, one step
//     before the corruption, and it is what `--atlas-stress` and the soak
//     column drive.
//
// WHAT IT DOES NOT CATCH: a partial drop inside an ordinary measurement on a
// frame where probe() did not run. Detecting that from outside the library
// needs a known-good width for the exact string, which is #350.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "prof.h"

namespace hanabi::atlas {

enum class Fault {
    None,
    // A non-blank string measured 0 or less.
    ZeroWidth,
    // A width that is NaN or infinite.
    NotFinite,
    // probe() asked for a glyph the atlas had never held and got nothing back.
    AtlasFull,
};

inline const char* fault_name(Fault f) {
    switch (f) {
        case Fault::ZeroWidth: return "ZERO_WIDTH";
        case Fault::NotFinite: return "NOT_FINITE";
        case Fault::AtlasFull: return "ATLAS_FULL";
        case Fault::None: break;
    }
    return "NONE";
}

struct State {
    // Measurement is only trustworthy once a font is loaded. Before that,
    // `measure_text` returns 0 by design (no context, no active font), and
    // treating the whole of launch as a fault would drown the
    // signal in the noise that made a silent atlas fill invisible.
    bool armed = false;
    unsigned long long faults = 0;
    unsigned long long prefont_zeros = 0;
    unsigned long long probes = 0;
    Fault first = Fault::None;
    std::string firstText;
    float firstPx = 0.0f;
    unsigned long long nextLogAt = 1;
};

inline State& state() {
    static State s;
    return s;
}

inline bool strict() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_ATLAS_STRICT");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

// Called once the font context exists and a face is loaded. Until then a zero
// width is "not ready", not "corrupt".
inline void arm() { state().armed = true; }
inline bool armed() { return state().armed; }

inline unsigned long long fault_count() { return state().faults; }
inline unsigned long long prefont_zero_count() { return state().prefont_zeros; }
inline Fault first_fault() { return state().first; }
inline const std::string& first_fault_text() { return state().firstText; }
inline float first_fault_px() { return state().firstPx; }

inline void reset_for_test() {
    State& s = state();
    s.faults = 0;
    s.prefont_zeros = 0;
    s.probes = 0;
    s.first = Fault::None;
    s.firstText.clear();
    s.firstPx = 0.0f;
    s.nextLogAt = 1;
}

inline void raise(Fault f, std::string_view text, float px) {
    State& s = state();
    ++s.faults;
    if (s.first == Fault::None) {
        s.first = f;
        s.firstText.assign(text.substr(0, 64));
        s.firstPx = px;
    }
    prof::tick("text.atlas_fault");
    // 1, 2, 4, 8, ... Once the atlas is full every frame faults, and a line
    // per measurement per frame is a line nobody reads.
    if (s.faults >= s.nextLogAt) {
        s.nextLogAt *= 2;
        std::fprintf(
            stderr,
            "[atlas] GLYPH ATLAS FAULT (%s) #%llu: measuring %.1fpt \"%.40s\" "
            "returned a width that cannot be true. The known cause is the "
            "2048x2048 font atlas being full: fontstash drops the glyph AND "
            "its advance, so every wrap, hug, ellipsis and spacer computed "
            "from this is wrong. afterhours_gaps.md #350/#353.\n",
            fault_name(f), s.faults, static_cast<double>(px),
            std::string(text.substr(0, 40)).c_str());
        std::fflush(stderr);
    }
    if (strict()) {
        std::fprintf(stderr,
                     "[atlas] HANABI_ATLAS_STRICT=1: aborting on the first "
                     "measurement fault.\n");
        std::fflush(stderr);
        std::abort();
    }
}

// Does `text` contain anything that must have width?
inline bool must_have_width(std::string_view text) {
    for (unsigned char c : text)
        if (c > 0x20) return true;
    return false;
}

// The seam. Hand it what was measured; it hands the same number back, having
// said something if the number cannot be true.
inline float check(std::string_view text, float px, float w) {
    if (!must_have_width(text)) return w;
    if (!std::isfinite(w)) {
        raise(Fault::NotFinite, text, px);
        return w;
    }
    if (w > 0.0f) return w;
    if (!state().armed) {
        ++state().prefont_zeros;
        return w;
    }
    raise(Fault::ZeroWidth, text, px);
    return w;
}

// Ask the atlas directly whether it can still take a glyph.
//
// `measure` is `(const char* text, float px) -> float`. The probe uses a
// (codepoint, size) pair the atlas cannot already hold: fontstash keys a glyph
// on (codepoint, size*10, blur), so bumping the size by a tenth of a point on
// each call guarantees a genuinely new rect every time. `px_hint` should be the
// largest size the caller cares about -- the allocator is a skyline, so a small
// rect can still fit long after a large one cannot, and probing at 10pt would
// answer a question nobody asked.
//
// The probe consumes atlas space itself, which is the reason it is not called
// from a frame: at a tenth of a point per call it is a few hundred bytes a
// time, fine for a soak sample or a gate, wrong for 120 Hz.
template <class Measure>
inline bool probe(float px_hint, Measure&& measure) {
    State& s = state();
    ++s.probes;
    const float px =
        px_hint + 0.1f * static_cast<float>(s.probes % 500 + 1);
    const float w = measure("M", px);
    prof::tick("text.atlas_probe");
    if (std::isfinite(w) && w > 0.0f) return true;
    raise(Fault::AtlasFull, "M", px);
    return false;
}

inline unsigned long long probe_count() { return state().probes; }

}  // namespace hanabi::atlas
