#pragma once

// ---------------------------------------------------------------------------
// A measurement that came back WRONG must not be allowed to be silent.
//
// THE CONDITION. afterhours' sokol backend creates one fontstash atlas of
// 2048x2048 R8 at init (`backends/sokol/backend.h:104`) and it never grows.
// When a glyph will not fit, fontstash raises FONS_ATLAS_FULL through
// `stash->handleError`; afterhours never calls `fonsSetErrorCallback`, so the
// default handler is null, `fons__getGlyph` returns NULL, and
// `fonsTextBounds` -- which is what `measure_text` and
// `measure_text_internal` are -- simply DOES NOT ADVANCE for that glyph.
// afterhours_gaps.md #211, measured:
//
//     size 144 pt   width 5933.0
//     size 192 pt   width  230.0
//     size 288 pt   width    0.0
//
// No error, no log, no exception, no return code. This is not a rendering
// artefact somebody squints at: `measure_text` is the input to every wrap,
// every hug-to-text, every ellipsize and every virtualization spacer in this
// app, so a string that measures short is LAID OUT short and a string that
// measures zero is laid out as absent. And hanabi now measures through memos
// (src/util/text_cache.h, src/util/wrap_count.h), so a poisoned number can be
// remembered as well as used.
//
// WHY IT IS DETECTED HERE RATHER THAN FIXED. vendor/afterhours is read-only in
// this repo. The atlas size is hard-coded two lines above the `sfons_create`
// call, the FONScontext lives in a backend-private static with no accessor, so
// `fonsSetErrorCallback` is out of reach, and nothing reports atlas occupancy.
// The one thing a consumer CAN do is refuse to trust the answer, which is what
// this file is. Filed upstream as gaps #350-#353.
//
// WHAT IT CATCHES, HONESTLY
//
//   * ZERO. A non-blank string that measures 0 is the terminal symptom, and it
//     is exact: every printable glyph in every font hanabi ships has a
//     non-zero advance, so zero can only mean "no glyph was accounted for".
//   * NOT FINITE. A NaN width poisons a comparison rather than a layout, which
//     is worse, and costs the same one branch to catch.
//   * SHORT, via `probe()`. A PARTIAL drop -- 5933 where the truth is ~7900 --
//     cannot be recognised from the number alone by anybody who does not
//     already know the answer, so it is not guessed at. Instead `probe()` asks
//     the question directly: it measures a glyph the atlas has never been
//     asked for, at a size the atlas has never been asked for, and a zero
//     advance from THAT means the atlas can no longer accept a new rect. It is
//     the condition, one step before the corruption, and it is what the
//     `--atlas-stress` mode and the soak column drive.
//
// WHAT IT DOES NOT CATCH, said plainly: a partial drop inside an ordinary
// measurement, on a frame where `probe()` did not run. Detecting that from
// outside the library needs a known-good width for the exact string, which no
// consumer has. The probe bounds how long the condition can go unnoticed; it
// does not make every individual poisoned measurement identifiable.
//
// COST. `check()` is one compare on a float the caller already has, and the
// caller already branched on it in the fallback path. `probe()` is one
// measurement, and it is called from the soak sampler and the stress mode, not
// from a frame.
//
// LOUDNESS, in four places, because the whole failure of #211 is silence:
//   * a line on stderr, ALWAYS, on the first fault and then at a decaying rate
//     (a fault repeats every frame once the atlas is full, and a log that
//     scrolls a build log off the screen gets muted, which is how this class
//     of bug survives);
//   * `hanabi::atlas::fault_count()` and `first_fault()` -- the test hook;
//   * `hanabi::prof` counters, so HANABI_PROF=1 and the perf gates see it;
//   * a `text-faults` column on every soak line;
//   * and `HANABI_ATLAS_STRICT=1` turns the first fault into an abort, which
//     is what a debug run and scripts/atlas_gate.sh use.
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
    // `measure_text_internal` returns 0 by design (no context, no active
    // font), and treating the whole of launch as a fault would drown the
    // signal in the noise that made #211 invisible in the first place.
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
            "from this is wrong. afterhours_gaps.md #211/#350.\n",
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
