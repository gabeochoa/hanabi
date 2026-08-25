#pragma once

// ---------------------------------------------------------------------------
// RUN UNTIL IT BREAKS, and say where.
//
// Every scenario in util/stress.h terminates on a fixed COUNT, and that is
// deliberate and stays: a count is the same amount of work on a fast machine
// and a slow one, so two runs are comparable. But a fixed count can only ever
// answer "did this stay flat for N frames". The reported bug was "it gets
// slower and slower every second until it FREEZES", and the question that
// matches it is a different one: *how far does it get?*
//
// So a run can also be given a FAILURE CONDITION. It runs until something
// trips or until the frame cap, whichever comes first, and reports which, at
// what frame, and — the part that makes it actionable — in what STATE. "It
// broke at 118 open tabs" is a fact somebody can act on. "It broke at frame
// 4720" is not.
//
//     HANABI_STRESS_UNTIL="rss:600000,blocks:400000,cpu:3.0"
//
// Comma-separated, any subset, all optional. With none set this whole file is
// a no-op and the run terminates on its count exactly as before.
//
//   rss:<KB>        resident size exceeds this many KB
//   blocks:<n>      live malloc blocks exceed this count
//   heap:<KB>       live malloc bytes exceed this many KB
//   entities:<n>    the ECS holds more than this many entities
//   tabs:<n>        the tab strip holds more than this many tabs
//   cpu:<ratio>     a frame costs more than ratio x the SETTLED BASELINE of
//                   thread CPU time
//
//   HANABI_STRESS_UNTIL_SUSTAIN=<n>   frames a condition must hold before it
//                                     counts as broken (default 30)
//
// A CONDITION HAS TO HOLD, NOT JUST HAPPEN. The first version tripped on any
// single frame and `cpu:3.0` fired at frame 1 of 30000 -- one 4.6 ms frame
// against a 1.4 ms baseline, which is a tab opening, not a freeze. Thirty
// consecutive frames is half a second of the app being that slow, which is
// what "it gets slower until it freezes" means and what a single hiccup is
// not. The memory conditions are monotone so the requirement costs them
// nothing; it is applied to all of them so there is one rule to remember.
//
// WHY cpu IS A RATIO AND EVERYTHING ELSE IS ABSOLUTE. This box is shared and
// its load average has hit 29, so an absolute millisecond threshold is a coin
// flip -- the repo rule is "never gate on absolute milliseconds". A ratio
// against a baseline measured on the same machine in the same minute divides
// the machine out. Memory has no such problem: a byte is a byte whoever else
// is running, so an absolute ceiling on RSS or on a block count means the
// same thing on a quiet box and a loaded one.
//
// The baseline is the MEDIAN frame CPU over the settle pass, not the mean: the
// settle carries a launch burst and one 40 ms frame would put the baseline
// somewhere no later frame could reach, which turns the condition off without
// saying so.
//
// A RUN THAT DID NOT BREAK IS NOT A PASS. It reports SURVIVED, with what it
// survived -- the frame count and the state it reached. A ceiling nothing
// came near is a condition that was never tested, and that is worth knowing
// before quoting the run as evidence of anything.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace hanabi::breaker {

struct Conditions {
    long rssKb = 0;        // 0 = not armed, for every field
    long blocks = 0;
    long heapKb = 0;
    long entities = 0;
    long tabs = 0;
    double cpuRatio = 0.0;

    [[nodiscard]] bool any() const {
        return rssKb > 0 || blocks > 0 || heapKb > 0 || entities > 0 ||
               tabs > 0 || cpuRatio > 0.0;
    }
};

// What the run reached when it stopped, whatever stopped it.
struct State {
    int frame = 0;
    long rssKb = 0;
    long blocks = 0;
    long heapKb = 0;
    long entities = 0;
    long tabs = 0;
    double cpuMs = 0.0;
    double cpuBaselineMs = 0.0;
};

struct Verdict {
    bool broke = false;
    // How many consecutive frames the condition had held when it tripped.
    int held = 0;
    // Which condition tripped, e.g. "tabs". Empty when nothing did.
    std::string condition;
    std::string detail;
    State at;
};

inline Conditions parse(const char* spec) {
    Conditions c;
    if (spec == nullptr || *spec == '\0') return c;
    const std::string s{spec};
    size_t i = 0;
    while (i < s.size()) {
        const size_t comma = s.find(',', i);
        const std::string term =
            s.substr(i, comma == std::string::npos ? std::string::npos
                                                   : comma - i);
        const size_t colon = term.find(':');
        if (colon != std::string::npos) {
            const std::string key = term.substr(0, colon);
            const std::string val = term.substr(colon + 1);
            // An unrecognised key is reported, not ignored. A typo'd condition
            // silently arming nothing is the failure mode this whole file is
            // trying to avoid, one level up.
            if (key == "rss") c.rssKb = std::atol(val.c_str());
            else if (key == "blocks") c.blocks = std::atol(val.c_str());
            else if (key == "heap") c.heapKb = std::atol(val.c_str());
            else if (key == "entities") c.entities = std::atol(val.c_str());
            else if (key == "tabs") c.tabs = std::atol(val.c_str());
            else if (key == "cpu") c.cpuRatio = std::strtod(val.c_str(), nullptr);
            else
                std::printf("[break] UNKNOWN CONDITION '%s' in "
                            "HANABI_STRESS_UNTIL -- it is armed on nothing. "
                            "Known: rss, blocks, heap, entities, tabs, cpu.\n",
                            key.c_str());
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return c;
}

inline Conditions from_env() {
    static const Conditions c = parse(std::getenv("HANABI_STRESS_UNTIL"));
    return c;
}

inline int sustain_frames() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_STRESS_UNTIL_SUSTAIN");
        const int parsed = (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
        return parsed > 0 ? parsed : 30;
    }();
    return n;
}

// Consecutive frames the current condition has held. Reset whenever nothing
// is tripping, or whenever a DIFFERENT condition starts tripping -- twenty
// frames of `cpu` followed by twenty of `rss` is not forty frames of
// anything, and counting it as one would name the wrong cause.
struct Streak {
    std::string condition;
    int frames = 0;
};

// Test the armed conditions against a state. First trip wins; the order is
// memory, then structure, then time, because a memory ceiling is the least
// ambiguous thing to have hit.
inline Verdict test_instant(const Conditions& c, const State& s) {
    Verdict v;
    v.at = s;
    char buf[256];
    auto trip = [&](const char* name, const char* fmt, auto value, auto limit) {
        v.broke = true;
        v.condition = name;
        std::snprintf(buf, sizeof(buf), fmt, value, limit);
        v.detail = buf;
    };
    if (c.rssKb > 0 && s.rssKb > c.rssKb)
        trip("rss", "RSS reached %ld KB, past the %ld KB ceiling", s.rssKb,
             c.rssKb);
    else if (c.heapKb > 0 && s.heapKb > c.heapKb)
        trip("heap", "live heap reached %ld KB, past the %ld KB ceiling",
             s.heapKb, c.heapKb);
    else if (c.blocks > 0 && s.blocks > c.blocks)
        trip("blocks", "live blocks reached %ld, past the %ld ceiling",
             s.blocks, c.blocks);
    else if (c.entities > 0 && s.entities > c.entities)
        trip("entities", "the ECS reached %ld entities, past the %ld ceiling",
             s.entities, c.entities);
    else if (c.tabs > 0 && s.tabs > c.tabs)
        trip("tabs", "the strip reached %ld tabs, past the %ld ceiling", s.tabs,
             c.tabs);
    else if (c.cpuRatio > 0.0 && s.cpuBaselineMs > 0.0 &&
             s.cpuMs > s.cpuBaselineMs * c.cpuRatio) {
        v.broke = true;
        v.condition = "cpu";
        std::snprintf(buf, sizeof(buf),
                      "a frame cost %.3f ms of thread CPU, %.2fx the settled "
                      "baseline of %.3f ms (ceiling %.2fx)",
                      s.cpuMs, s.cpuMs / s.cpuBaselineMs, s.cpuBaselineMs,
                      c.cpuRatio);
        v.detail = buf;
    }
    return v;
}

// The armed check a caller actually uses: an instantaneous trip only counts
// once it has held for `sustain_frames()` consecutive frames.
inline Verdict test(const Conditions& c, const State& s, Streak& streak) {
    Verdict v = test_instant(c, s);
    if (!v.broke) {
        streak.condition.clear();
        streak.frames = 0;
        return v;
    }
    if (streak.condition != v.condition) {
        streak.condition = v.condition;
        streak.frames = 0;
    }
    ++streak.frames;
    v.held = streak.frames;
    if (streak.frames < sustain_frames()) {
        // Tripping but not yet sustained: not broken, and the caller must not
        // read `at` as an end state.
        v.broke = false;
    }
    return v;
}

// The median of the settle pass's per-frame CPU, which is what `cpu:<ratio>`
// is a ratio OF. Median and not mean: the settle carries the launch burst, and
// one 40 ms frame in a mean puts the baseline somewhere no later frame can
// reach, which disarms the condition without saying so.
inline double median_of(std::vector<double> xs) {
    if (xs.empty()) return 0.0;
    const size_t mid = xs.size() / 2;
    std::nth_element(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(mid),
                     xs.end());
    return xs[mid];
}

inline void report(const Conditions& c, const Verdict& v, int framesRun,
                   int frameCap, const char* scenario, const char* work) {
    if (!c.any()) return;
    std::printf("\n[break] ---------------- RUN UNTIL IT BREAKS -----------\n");
    std::printf("[break] scenario %s, cap %d frames\n", scenario, frameCap);
    if (v.broke) {
        std::printf("[break] BROKE at frame %d of %d, on `%s`, after holding "
                    "for %d consecutive\n[break] frames (the requirement is "
                    "%d -- a single frame is a hiccup, not a break).\n",
                    v.at.frame, frameCap, v.condition.c_str(), v.held,
                    sustain_frames());
        std::printf("[break]   %s\n", v.detail.c_str());
    } else {
        // Not a pass, and the wording has to stop it being read as one.
        std::printf("[break] SURVIVED %d frames. NOTHING TRIPPED, which is not "
                    "the same as\n[break] passing: it means every ceiling was "
                    "set above where this run got.\n",
                    framesRun);
    }
    std::printf("[break] State at the end: frame %d, RSS %ld KB, heap %ld KB, "
                "%ld live blocks,\n[break]   %ld entities, %ld tabs, frame CPU "
                "%.3f ms against a %.3f ms baseline.\n",
                v.at.frame, v.at.rssKb, v.at.heapKb, v.at.blocks, v.at.entities,
                v.at.tabs, v.at.cpuMs, v.at.cpuBaselineMs);
    std::printf("[break] The scenario did: %s\n", work);
    if (v.broke)
        std::printf("[break]\n[break] The per-bucket table above is the "
                    "trajectory it took to get there.\n[break] Re-run with a "
                    "smaller HANABI_SOAK_EVERY for a finer one, or with the "
                    "same\n[break] ceiling and HANABI_STRESS_SESSIONS raised "
                    "to see whether the wall moves\n[break] with the catalog "
                    "or stays where it is.\n");
    std::printf("[break] ------------------------------------------------\n");
    std::fflush(stdout);
}

}  // namespace hanabi::breaker
