#pragma once

// ---------------------------------------------------------------------------
// The launch curve: what the FIRST frames cost, one by one.
//
// afterhours_gaps.md #155 measured this once, by hand, off the [hprof] marks:
// three settle frames costing 6.3-10.7 ms each against a warm frame of 1-2 ms,
// so the first draws are 5-8x. That number came from subtracting two cumulative
// timestamps and dividing by three, which cannot say whether it is frame 0
// that is expensive or all three equally -- and those are different bugs with
// different fixes.
//
//   HANABI_LAUNCH_CURVE=<n>   record the first n frames individually, then
//                             report each one against the warm median
//
// A hard no-op when unset, the same contract as soak.h and prof.h.
//
// WHY BOTH CLOCKS. Thread CPU time (CLOCK_THREAD_CPUTIME_ID) counts only
// cycles this thread was given, so a neighbouring build steals throughput but
// not the measurement -- docs/perf/STARTUP.md is emphatic about this and the
// box proves it hourly. Metal compiles a pipeline state on the CALLING thread,
// so the cost this file exists to find is inside that clock. Wall clock is
// reported beside it because it is what a person waits, and because a gap
// between the two is itself a finding: it is the driver doing work somewhere
// this thread is not.
//
// WHY THE HEADLINE IS A RATIO. This box has run at load average 29 with three
// other agents building on it. An absolute millisecond is a measurement of the
// machine; frame 0 divided by the warm median is a measurement of the app, and
// both halves are taken seconds apart in the same process.
// ---------------------------------------------------------------------------

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hanabi::launch_curve {

// Frames recorded individually, or 0 when the probe is off. Read once.
inline int frames() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_LAUNCH_CURVE");
        if (v == nullptr || *v == '\0') return 0;
        const int parsed = std::atoi(v);
        return parsed > 0 ? parsed : 16;
    }();
    return n;
}

inline bool enabled() { return frames() > 0; }

inline unsigned long long cpu_nanos() {
    struct timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec);
}

struct Row {
    std::string phase;
    double cpuMs = 0.0;
    double wallMs = 0.0;
};

inline std::vector<Row>& rows() {
    static std::vector<Row> r;
    return r;
}

// Frames drawn after the recorded window, kept only as a median: the warm
// steady state every early frame is measured against.
inline std::vector<double>& warm_cpu() {
    static std::vector<double> w;
    return w;
}
inline std::vector<double>& warm_wall() {
    static std::vector<double> w;
    return w;
}

// RAII around one frame's begin/draw/end. `phase` says which loop it came
// from, because "the first frame" is ambiguous in this app: the settle loop
// draws several before the capture loop's frame 0, and #155's whole complaint
// is that the warm-up lands in whichever of them ran first.
struct Frame {
    const char* phase;
    unsigned long long cpu0;
    std::chrono::high_resolution_clock::time_point wall0;

    explicit Frame(const char* p)
        : phase(p),
          cpu0(enabled() ? cpu_nanos() : 0),
          wall0(std::chrono::high_resolution_clock::now()) {}

    ~Frame() {
        if (!enabled()) return;
        const double cpuMs =
            static_cast<double>(cpu_nanos() - cpu0) / 1000000.0;
        const double wallMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - wall0)
                .count();
        if (static_cast<int>(rows().size()) < frames()) {
            rows().push_back(Row{phase, cpuMs, wallMs});
        } else {
            warm_cpu().push_back(cpuMs);
            warm_wall().push_back(wallMs);
        }
    }
};

inline double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

inline void report() {
    if (!enabled()) return;
    if (rows().empty()) {
        std::printf("[curve] no frames recorded\n");
        return;
    }
    const double warmCpu = median(warm_cpu());
    const double warmWall = median(warm_wall());
    std::printf("\n[curve] the first %zu frames, against the warm median of "
                "%zu later ones\n", rows().size(), warm_cpu().size());
    std::printf("[curve]   %-4s %-18s %10s %8s %10s\n", "#", "phase",
                "cpu ms", "xwarm", "wall ms");
    for (size_t i = 0; i < rows().size(); ++i) {
        const Row& r = rows()[i];
        char x[16];
        if (warmCpu > 0.0)
            std::snprintf(x, sizeof(x), "%.2fx", r.cpuMs / warmCpu);
        else
            std::snprintf(x, sizeof(x), "%s", "-");
        std::printf("[curve]   %-4zu %-18s %10.3f %8s %10.3f\n", i,
                    r.phase.c_str(), r.cpuMs, x, r.wallMs);
    }
    std::printf("[curve]   %-4s %-18s %10.3f %8s %10.3f\n", "warm", "(median)",
                warmCpu, "1.00x", warmWall);

    // The two numbers worth quoting, both ratios, both load-independent.
    double sumCpu = 0.0;
    for (const Row& r : rows()) sumCpu += r.cpuMs;
    const double excess = sumCpu - warmCpu * static_cast<double>(rows().size());
    std::printf("[curve] frame 0 is %.2fx the warm frame; the first %zu "
                "frames cost\n[curve] %.1f ms of CPU above warm, which is the "
                "whole of what a pre-warm\n[curve] can move (afterhours_gaps.md "
                "#155).\n",
                warmCpu > 0.0 ? rows()[0].cpuMs / warmCpu : 0.0, rows().size(),
                excess);
    std::fflush(stdout);
}

}  // namespace hanabi::launch_curve
