#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "resize_drive.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <time.h>
#include <unistd.h>
#import <CoreGraphics/CoreGraphics.h>

#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <map>
#include <mutex>
#include <thread>

extern "C" unsigned metal_sync_draw_count(void);

namespace {

// A sampler for the MAIN thread, from inside the process: `sample(1)` cannot
// examine this process on the build machine (sandboxed, no sudo), and
// Instruments is not on it. Every 2 ms a helper thread suspends the main
// thread, reads its ARM64 state, walks the frame-pointer chain, resumes it,
// and files the stack. dladdr names each frame by its nearest exported
// symbol -- exact for AppKit/Metal/QuartzCore, nearest-static for this
// binary. Enabled by HANABI_RESIZE_DRIVE_SAMPLE=1; the report is the top
// stacks by count, leaf first, and what fraction of samples the app's own
// frame accounted for.
struct Sampler {
    mach_port_t main_thread = MACH_PORT_NULL;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> in_app_frame{false};
    unsigned total = 0, in_frame = 0, failed = 0;

    std::map<uintptr_t, std::string> names;
    // Samples are written into storage RESERVED up front (kMaxSamples of
    // kDepth+2 words) while the main thread is suspended: no allocation,
    // no symbolization, no lock the suspended thread could hold. dladdr
    // and the string work happen in finish(), after the last resume.
    static constexpr std::size_t kDepth = 40;
    static constexpr std::size_t kMaxSamples = 20000;
    std::vector<uintptr_t> flat;       // [n, in_frame, pcs...] per sample
    std::size_t sample_count = 0;
    bool full = false;

    std::string name_of(uintptr_t pc) {
        auto it = names.find(pc);
        if (it != names.end()) return it->second;
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_sname) {
            const char* img = info.dli_fname ? std::strrchr(info.dli_fname, '/') : nullptr;
            return std::string(img ? img + 1 : "?") + "`" + info.dli_sname;
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(pc));
        return buf;
    }
    std::string named(uintptr_t pc) {
        auto it = names.find(pc);
        if (it == names.end()) it = names.emplace(pc, name_of(pc)).first;
        return it->second;
    }

    void take() {
        if (full) return;
        if (flat.size() + kDepth + 2 > flat.capacity()) { full = true; return; }
        if (thread_suspend(main_thread) != KERN_SUCCESS) { ++failed; return; }
        arm_thread_state64_t state{};
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        uintptr_t pcs[kDepth];
        int n = 0;
        if (thread_get_state(main_thread, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state), &count) == KERN_SUCCESS) {
            pcs[n++] = static_cast<uintptr_t>(arm_thread_state64_get_pc(state));
            uintptr_t fp = static_cast<uintptr_t>(arm_thread_state64_get_fp(state));
            uintptr_t lr = static_cast<uintptr_t>(arm_thread_state64_get_lr(state));
            if (lr) pcs[n++] = lr & 0x0000007fffffffffULL;
            // Frame pointers: [fp] = previous fp, [fp+8] = return address.
            for (int i = 0; i < 36 && fp && n < static_cast<int>(kDepth); ++i) {
                if (fp & 0xf) break;
                if (fp < 0x100000000ULL || fp > 0x0000800000000000ULL) break;
                const uintptr_t* f = reinterpret_cast<const uintptr_t*>(fp);
                const uintptr_t next = f[0];
                const uintptr_t ret = f[1] & 0x0000007fffffffffULL;
                if (!ret) break;
                if (n >= 2 && ret == pcs[n - 1]) { fp = next; continue; }
                pcs[n++] = ret;
                if (next <= fp) break;
                fp = next;
            }
        }
        const bool app = in_app_frame.load();
        thread_resume(main_thread);
        ++total;
        if (app) ++in_frame;
        // Within reserved capacity (checked above): no allocation.
        flat.push_back(static_cast<uintptr_t>(n));
        flat.push_back(app ? 1u : 0u);
        for (int i = 0; i < n; ++i) flat.push_back(pcs[i]);
        ++sample_count;
    }

    // Walk the flat samples: f(pcs, n, in_frame).
    template <class F>
    void each(F&& f) const {
        std::size_t i = 0;
        while (i + 2 <= flat.size()) {
            const std::size_t n = flat[i];
            const bool app = flat[i + 1] != 0;
            if (i + 2 + n > flat.size()) break;
            f(&flat[i + 2], n, app);
            i += 2 + n;
        }
    }

    void start(mach_port_t main) {
        main_thread = main;
        flat.reserve(kMaxSamples * (kDepth + 2));
        worker = std::thread([this] {
            while (!stop.load()) {
                take();
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
    }
    void report(const char* title, bool only_in_frame, int depth, unsigned limit) {
        std::map<std::string, unsigned> agg;
        unsigned pop = 0;
        each([&](const uintptr_t* pcs, std::size_t n, bool app) {
            if (only_in_frame && !app) return;
            ++pop;
            std::string key;
            for (std::size_t k = 0; k < n && k < static_cast<std::size_t>(depth); ++k) {
                if (!key.empty()) key += " < ";
                key += named(pcs[k]);
            }
            ++agg[key];
        });
        std::vector<std::pair<unsigned, std::string>> v;
        for (auto& [k, c] : agg) v.emplace_back(c, k);
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::printf("[resize-drive] %s population=%u\n", title, pop);
        unsigned shown = 0;
        for (auto& [c, k] : v) {
            if (shown++ >= limit) break;
            std::printf("[resize-drive] %5u %5.1f%%  %s\n", c, pop ? 100.0 * c / pop : 0.0, k.c_str());
        }
    }
    // The app's own frame, by the first hanabi.exe function on the stack
    // above the libraries: where the frame's CPU goes, phase by phase.
    void report_app_functions(unsigned limit) {
        std::map<std::string, unsigned> agg;
        unsigned pop = 0;
        each([&](const uintptr_t* pcs, std::size_t n, bool app) {
            if (!app) return;
            ++pop;
            std::string leaf, owner;
            for (std::size_t k = 0; k < n; ++k) {
                const std::string nm = named(pcs[k]);
                if (leaf.empty()) leaf = nm;
                if (nm.rfind("hanabi.exe`", 0) == 0) { owner = nm; break; }
            }
            ++agg[owner.empty() ? leaf : owner + "   (leaf " + leaf + ")"];
        });
        std::vector<std::pair<unsigned, std::string>> v;
        for (auto& [k, c] : agg) v.emplace_back(c, k);
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::printf("[resize-drive] APP-FRAME by first app function population=%u\n", pop);
        unsigned shown = 0;
        for (auto& [c, k] : v) {
            if (shown++ >= limit) break;
            std::printf("[resize-drive] %5u %5.1f%%  %s\n", c, pop ? 100.0 * c / pop : 0.0, k.c_str());
        }
    }
    void finish() {
        stop.store(true);
        if (worker.joinable()) worker.join();
        std::printf("[resize-drive] SAMPLES total=%u in_app_frame=%u (%.0f%%) failed=%u%s\n", total, in_frame,
                    total ? 100.0 * in_frame / total : 0.0, failed, full ? " (storage full, sampling stopped)" : "");
        report("MAIN-THREAD stacks (all samples, 14 deep)", false, 14, 12);
        report("APP-FRAME stacks (in-frame samples, 8 deep)", true, 8, 30);
        report_app_functions(30);
        std::fflush(stdout);
    }
};

Sampler* g_sampler = nullptr;

struct Pattern {
    enum class Kind { Grow, Hold } kind = Kind::Hold;
    int dw = 0, dh = 0, steps = 0;
};

struct Sample {
    double t_ms;       // since arm
    int win_w, win_h;  // NSWindow content size, points
    int fb_w, fb_h;    // drawable size, pixels
    double cpu_ms;     // thread CPU inside the app's frame
    double wall_ms;    // wall inside the app's frame
    bool live;         // inside windowWillStart..DidEndLiveResize
    unsigned notes;    // windowDidResize notifications since the last frame
    unsigned callbacks;  // backend frame callbacks since the last frame (incl. this one)
};

struct State {
    bool armed = false;
    bool installed = false;
    bool finished = false;
    std::vector<Pattern> patterns;
    std::size_t pattern_ix = 0;
    int step_ix = 0;
    bool mouse_down = false;
    NSPoint corner_screen{};      // where the pointer is, screen coords
    NSPoint start_corner_screen{};
    NSSize start_size{};
    int tick_ms = 8;
    std::chrono::steady_clock::time_point t0;
    std::atomic<unsigned> notes_since_frame{0};
    std::atomic<unsigned> notes_total{0};
    std::atomic<bool> live{false};
    unsigned live_starts = 0, live_ends = 0;
    int settle_ticks = 0;
    int rest_ticks = 0;
    std::atomic<unsigned> frames_since_live_end{0};
    std::vector<Sample> samples;
    std::atomic<unsigned> callbacks_total{0};
    std::atomic<unsigned> callbacks_since_frame{0};
    std::vector<double> callback_t;   // ms since arm, every callback
    std::vector<double> note_t;       // ms since arm, every windowDidResize
    std::vector<double> event_t;      // ms since arm, every posted drag event
    std::mutex times_mu;
    unsigned long long frame_cpu0 = 0;
    std::chrono::steady_clock::time_point frame_wall0;
    id observer = nil;
    std::thread driver;
    int events_posted = 0;
    unsigned frames_before_down = 0;
};

State& st() {
    static State s;
    return s;
}

unsigned long long thread_cpu_ns() {
    struct timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec);
}

double ms_since(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - a)
        .count();
}

bool parse(const char* spec, std::vector<Pattern>& out) {
    std::string s(spec);
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        i = j + 1;
        Pattern p;
        int dw = 0, dh = 0, n = 0;
        if (std::sscanf(part.c_str(), "grow:%dx%d:%d", &dw, &dh, &n) == 3 && n > 0) {
            p.kind = Pattern::Kind::Grow;
            p.dw = dw;
            p.dh = dh;
            p.steps = n;
        } else if (std::sscanf(part.c_str(), "hold:%d", &n) == 1 && n > 0) {
            p.kind = Pattern::Kind::Hold;
            p.steps = n;
        } else {
            std::fprintf(stderr, "[resize-drive] bad pattern '%s'\n", part.c_str());
            return false;
        }
        out.push_back(p);
    }
    return !out.empty();
}

NSWindow* the_window() {
    NSWindow* w = [NSApp mainWindow];
    if (!w) w = [NSApp keyWindow];
    if (!w)
        for (NSWindow* c in [NSApp windows])
            if ([c isVisible]) return c;
    return w;
}

// HANABI_RESIZE_DRIVE_VIA=cg: post through the window server with
// CGEventPostToPid(getpid()) -- the path a real mouse's events take into the
// app -- instead of NSApp's queue. On a machine that has not granted this
// process Accessibility the events are dropped silently; the report's
// live_starts=0 says so.
bool via_cg() {
    static const bool v = [] {
        const char* e = std::getenv("HANABI_RESIZE_DRIVE_VIA");
        return e && std::strcmp(e, "cg") == 0;
    }();
    return v;
}

void post_mouse(NSEventType type, NSWindow* win, NSPoint screen_pt, int n) {
    if (via_cg()) {
        // CG uses a top-left origin on the main display.
        const CGFloat screenH = [[[NSScreen screens] firstObject] frame].size.height;
        const CGPoint p = CGPointMake(screen_pt.x, screenH - screen_pt.y);
        CGEventType t = kCGEventLeftMouseDragged;
        if (type == NSEventTypeLeftMouseDown) t = kCGEventLeftMouseDown;
        if (type == NSEventTypeLeftMouseUp) t = kCGEventLeftMouseUp;
        CGEventRef e = CGEventCreateMouseEvent(nullptr, t, p, kCGMouseButtonLeft);
        CGEventPostToPid(getpid(), e);
        CFRelease(e);
        if (type == NSEventTypeLeftMouseDragged) {
            State& s = st();
            std::lock_guard<std::mutex> g(s.times_mu);
            s.event_t.push_back(ms_since(s.t0));
        }
        return;
    }
    // Window base coordinates, bottom-left origin, from the CURRENT frame.
    NSRect wf = [win frame];
    NSPoint loc = NSMakePoint(screen_pt.x - wf.origin.x, screen_pt.y - wf.origin.y);
    NSEvent* e = [NSEvent mouseEventWithType:type
                                    location:loc
                               modifierFlags:0
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:[win windowNumber]
                                     context:nil
                                 eventNumber:n
                                  clickCount:1
                                    pressure:type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
    [NSApp postEvent:e atStart:NO];
    if (type == NSEventTypeLeftMouseDragged) {
        State& s = st();
        std::lock_guard<std::mutex> g(s.times_mu);
        s.event_t.push_back(ms_since(s.t0));
    }
}

std::vector<double>& tick_gaps() { static std::vector<double> v; return v; }

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double k = (p / 100.0) * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(k);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (k - static_cast<double>(lo));
}

void summarize() {
    State& s = st();
    std::vector<double> cpu, wall, gaps;
    unsigned missed_sizes = 0;  // notifications beyond one per frame
    unsigned live_frames = 0;
    for (std::size_t i = 0; i < s.samples.size(); ++i) {
        const Sample& x = s.samples[i];
        if (!x.live) continue;
        ++live_frames;
        cpu.push_back(x.cpu_ms);
        wall.push_back(x.wall_ms);
        if (x.notes > 1) missed_sizes += x.notes - 1;
        if (i > 0 && s.samples[i - 1].live) gaps.push_back(x.t_ms - s.samples[i - 1].t_ms);
    }
    // Cadence of the three clocks during the drag: how often the backend
    // called back, how often AppKit applied a size, how often we posted.
    std::vector<double> cb_gaps, note_gaps, ev_gaps;
    {
        std::lock_guard<std::mutex> g(s.times_mu);
        for (std::size_t i = 1; i < s.callback_t.size(); ++i) cb_gaps.push_back(s.callback_t[i] - s.callback_t[i - 1]);
        for (std::size_t i = 1; i < s.note_t.size(); ++i) note_gaps.push_back(s.note_t[i] - s.note_t[i - 1]);
        for (std::size_t i = 1; i < s.event_t.size(); ++i) ev_gaps.push_back(s.event_t[i] - s.event_t[i - 1]);
    }
    std::printf("[resize-drive] CADENCE callbacks=%u (gap p50=%.1f p95=%.1f max=%.1f) "
                "resize_notes=%zu (gap p50=%.1f p95=%.1f max=%.1f) "
                "drag_events=%zu (gap p50=%.1f p95=%.1f max=%.1f)\n",
                s.callbacks_total.load(), pct(cb_gaps, 50), pct(cb_gaps, 95),
                cb_gaps.empty() ? 0.0 : *std::max_element(cb_gaps.begin(), cb_gaps.end()),
                s.note_t.size(), pct(note_gaps, 50), pct(note_gaps, 95),
                note_gaps.empty() ? 0.0 : *std::max_element(note_gaps.begin(), note_gaps.end()),
                s.event_t.size(), pct(ev_gaps, 50), pct(ev_gaps, 95),
                ev_gaps.empty() ? 0.0 : *std::max_element(ev_gaps.begin(), ev_gaps.end()));
    // The settled size is read from the WINDOW now, not from the last frame:
    // a mouse-up ends the live resize without a size change, so the last
    // rendered frame can predate it.
    int finalW = 0, finalH = 0;
    if (NSWindow* win = the_window()) {
        const NSSize cs = [[win contentView] frame].size;
        finalW = static_cast<int>(cs.width);
        finalH = static_cast<int>(cs.height);
    }
    unsigned frames_after_live_end = 0;
    for (auto it = s.samples.rbegin(); it != s.samples.rend() && !it->live; ++it) ++frames_after_live_end;
    const Sample* last = s.samples.empty() ? nullptr : &s.samples.back();
    std::printf(
        "[resize-drive] SUMMARY frames_live=%u notes=%u live_starts=%u live_ends=%u "
        "events=%d cpu_p50=%.2f cpu_p95=%.2f cpu_p99=%.2f cpu_max=%.2f "
        "wall_p50=%.2f wall_p95=%.2f wall_max=%.2f gap_p50=%.2f gap_p95=%.2f gap_max=%.2f "
        "sizes_skipped=%u final=%dx%d last_frame=%dx%d fb=%dx%d settle_frames=%u sync_draws=%u tick_p50=%.1f tick_p95=%.1f tick_max=%.1f\n",
        live_frames, s.notes_total.load(), s.live_starts, s.live_ends, s.events_posted,
        pct(cpu, 50), pct(cpu, 95), pct(cpu, 99), cpu.empty() ? 0.0 : *std::max_element(cpu.begin(), cpu.end()),
        pct(wall, 50), pct(wall, 95), wall.empty() ? 0.0 : *std::max_element(wall.begin(), wall.end()),
        pct(gaps, 50), pct(gaps, 95), gaps.empty() ? 0.0 : *std::max_element(gaps.begin(), gaps.end()),
        missed_sizes, finalW, finalH, last ? last->win_w : 0, last ? last->win_h : 0, last ? last->fb_w : 0,
        last ? last->fb_h : 0, frames_after_live_end, metal_sync_draw_count(),
        pct(tick_gaps(), 50), pct(tick_gaps(), 95),
        tick_gaps().empty() ? 0.0 : *std::max_element(tick_gaps().begin(), tick_gaps().end()));
    std::fflush(stdout);
}

void tick() {
    State& s = st();
    if (s.finished) return;
    {
        static std::chrono::steady_clock::time_point last{};
        const auto now = std::chrono::steady_clock::now();
        if (last.time_since_epoch().count() != 0)
            tick_gaps().push_back(std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
    }
    NSWindow* win = the_window();
    if (!win) return;
    if (s.pattern_ix >= s.patterns.size()) {
        if (s.mouse_down) {
            post_mouse(NSEventTypeLeftMouseUp, win, s.corner_screen, ++s.events_posted);
            s.mouse_down = false;
            s.settle_ticks = 0;
            return;
        }
        // The settle: wait for AppKit to end the live resize (the mouse-up
        // has to be dequeued and processed), then for a frame drawn after
        // it, then a beat more so the summary reads the settled window.
        // Bounded: 2 s of ticks, after which the summary says live_ends=0.
        ++s.settle_ticks;
        const bool ended = !s.live.load();
        const bool framed = ended && s.frames_since_live_end.load() >= 2;
        if (!(framed || s.settle_ticks * s.tick_ms >= 2000)) return;
        s.finished = true;
        if (g_sampler) g_sampler->finish();
        summarize();
        return;
    }
    Pattern& p = s.patterns[s.pattern_ix];
    if (p.kind == Pattern::Kind::Grow) {
        if (!s.mouse_down) {
            // The bottom-right corner, three points inside the frame: the
            // theme frame's resize zone.
            NSRect wf = [win frame];
            s.start_corner_screen = NSMakePoint(NSMaxX(wf) - 3.0, NSMinY(wf) + 3.0);
            s.corner_screen = s.start_corner_screen;
            s.start_size = [[win contentView] frame].size;
            post_mouse(NSEventTypeLeftMouseDown, win, s.corner_screen, ++s.events_posted);
            s.mouse_down = true;
            s.step_ix = 0;
            return;
        }
        if (s.step_ix >= p.steps && s.rest_ticks < 3) {
            ++s.rest_ticks;
            post_mouse(NSEventTypeLeftMouseDragged, win, s.corner_screen, ++s.events_posted);
            return;
        }
        if (s.step_ix >= p.steps) {
            s.rest_ticks = 0;
            ++s.pattern_ix;
            if (s.pattern_ix < s.patterns.size() && s.patterns[s.pattern_ix].kind == Pattern::Kind::Grow) {
                s.start_corner_screen = s.corner_screen;
                s.step_ix = 0;
            }
            return;
        }
        ++s.step_ix;
        const double f = static_cast<double>(s.step_ix) / static_cast<double>(p.steps);
        // Bottom-right corner: +dw moves right, +dh moves DOWN (lower screen y).
        s.corner_screen = NSMakePoint(s.start_corner_screen.x + p.dw * f,
                                      s.start_corner_screen.y - p.dh * f);
        post_mouse(NSEventTypeLeftMouseDragged, win, s.corner_screen, ++s.events_posted);
        // At the last step the pointer rests there for three more ticks
        // (posted again above) before the pattern advances: the tracking
        // loop applies the size of the last DRAG it processed and a mouse-up
        // moves nothing, so a final position posted once can be the step it
        // coalesced past (measured: 1099x760 for a 1100x760 pointer).
        return;
    }
    // Hold: keep the button state as it is (a pause mid-drag). The settle
    // after the last pattern is handled above, once the patterns run out.
    if (++s.step_ix >= p.steps) {
        ++s.pattern_ix;
        s.step_ix = 0;
        if (s.pattern_ix < s.patterns.size() && s.patterns[s.pattern_ix].kind == Pattern::Kind::Grow) {
            // A grow after a hold starts a fresh press.
            if (s.mouse_down) {
                post_mouse(NSEventTypeLeftMouseUp, win, s.corner_screen, ++s.events_posted);
                s.mouse_down = false;
            }
        }
    }
}

}  // namespace

@interface HanabiResizeDriveObserver : NSObject
@end

@implementation HanabiResizeDriveObserver
- (void)willStart:(NSNotification*)n {
    (void)n;
    st().live.store(true);
    ++st().live_starts;
}
- (void)didEnd:(NSNotification*)n {
    (void)n;
    st().live.store(false);
    st().frames_since_live_end.store(0);
    ++st().live_ends;
}
- (void)didResize:(NSNotification*)n {
    (void)n;
    State& s = st();
    s.notes_since_frame.fetch_add(1);
    s.notes_total.fetch_add(1);
    std::lock_guard<std::mutex> g(s.times_mu);
    s.note_t.push_back(ms_since(s.t0));
}
@end

namespace hanabi::resize_drive {

bool armed() {
    static const bool a = [] {
        const char* v = std::getenv("HANABI_RESIZE_DRIVE");
        if (!v || !*v) return false;
        State& s = st();
        if (!parse(v, s.patterns)) return false;
        if (const char* t = std::getenv("HANABI_RESIZE_DRIVE_TICK_MS"); t && *t)
            s.tick_ms = std::max(1, std::atoi(t));
        s.armed = true;
        return true;
    }();
    return a;
}

void install() {
    if (!armed()) return;
    State& s = st();
    if (s.installed) return;
    NSWindow* win = the_window();
    if (!win) return;
    s.installed = true;
    s.t0 = std::chrono::steady_clock::now();
    HanabiResizeDriveObserver* o = [[HanabiResizeDriveObserver alloc] init];
    NSNotificationCenter* c = [NSNotificationCenter defaultCenter];
    [c addObserver:o selector:@selector(willStart:) name:NSWindowWillStartLiveResizeNotification object:win];
    [c addObserver:o selector:@selector(didEnd:) name:NSWindowDidEndLiveResizeNotification object:win];
    [c addObserver:o selector:@selector(didResize:) name:NSWindowDidResizeNotification object:win];
    s.observer = o;
    // A thread, not an NSTimer: measured on this machine an 8 ms NSTimer in
    // common modes fired every 29 ms (p50) with the app idle, 37 ms inside the
    // tracking loop -- coalesced -- which made the driver the slowest part of
    // the drag. A real mouse's events arrive on the window server port from
    // outside the run loop; a thread posting them does the same
    // (postEvent:atStart: is documented callable from any thread).
    s.driver = std::thread([] {
        // A mouse is user-interactive; without this the thread's sleeps are
        // coalesced (measured: 33-50 ms slop under nice -n 10) and the
        // "8 ms" cadence is fiction.
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        auto next = std::chrono::steady_clock::now();
        while (!st().finished) {
            next += std::chrono::milliseconds(st().tick_ms);
            std::this_thread::sleep_until(next);
            @autoreleasepool { tick(); }
        }
    });
    if (const char* sm = std::getenv("HANABI_RESIZE_DRIVE_SAMPLE"); sm && *sm && std::strcmp(sm, "0") != 0) {
        g_sampler = new Sampler();
        g_sampler->start(pthread_mach_thread_np(pthread_self()));
    }
    const NSSize cs = [[win contentView] frame].size;
    std::printf("[resize-drive] armed window=%dx%d tick=%dms patterns=%zu sampler=%d\n",
                static_cast<int>(cs.width), static_cast<int>(cs.height), s.tick_ms,
                s.patterns.size(), g_sampler ? 1 : 0);
    std::fflush(stdout);
}

void callback() {
    if (!armed()) return;
    State& s = st();
    if (!s.installed) return;
    s.callbacks_total.fetch_add(1);
    s.callbacks_since_frame.fetch_add(1);
    std::lock_guard<std::mutex> g(s.times_mu);
    s.callback_t.push_back(ms_since(s.t0));
}

void frame_begin() {
    if (!armed()) return;
    State& s = st();
    s.frame_cpu0 = thread_cpu_ns();
    s.frame_wall0 = std::chrono::steady_clock::now();
    if (g_sampler) g_sampler->in_app_frame.store(true);
}

void frame_end() {
    if (!armed()) return;
    State& s = st();
    if (!s.installed) return;
    NSWindow* win = the_window();
    if (!win) return;
    if (g_sampler) g_sampler->in_app_frame.store(false);
    {
        static std::string lastMode;
        NSString* m = [[NSRunLoop currentRunLoop] currentMode];
        std::string mode = m ? [m UTF8String] : "(none)";
        if (mode != lastMode) {
            std::printf("[resize-drive] frames now drawn in run loop mode %s\n", mode.c_str());
            lastMode = mode;
        }
    }
    Sample x{};
    x.t_ms = ms_since(s.t0);
    const NSSize cs = [[win contentView] frame].size;
    x.win_w = static_cast<int>(cs.width);
    x.win_h = static_cast<int>(cs.height);
    // The drawable size, from the Metal layer sokol renders into.
    CGSize ds = CGSizeZero;
    if (CALayer* layer = [[win contentView] layer]; [layer isKindOfClass:[CAMetalLayer class]])
        ds = [(CAMetalLayer*)layer drawableSize];
    x.fb_w = static_cast<int>(ds.width);
    x.fb_h = static_cast<int>(ds.height);
    x.cpu_ms = static_cast<double>(thread_cpu_ns() - s.frame_cpu0) / 1e6;
    x.wall_ms = ms_since(s.frame_wall0);
    x.live = s.live.load();
    if (!x.live && s.live_ends > 0) s.frames_since_live_end.fetch_add(1);
    x.notes = s.notes_since_frame.exchange(0);
    x.callbacks = s.callbacks_since_frame.exchange(0);
    s.samples.push_back(x);
    std::printf("[resize-drive] frame %zu t=%.1f win=%dx%d fb=%dx%d cpu=%.2f wall=%.2f live=%d notes=%u callbacks=%u\n",
                s.samples.size(), x.t_ms, x.win_w, x.win_h, x.fb_w, x.fb_h, x.cpu_ms, x.wall_ms,
                x.live ? 1 : 0, x.notes, x.callbacks);
}

bool finished() {
    State& s = st();
    if (s.finished && s.driver.joinable()) s.driver.join();
    return s.finished;
}

}  // namespace hanabi::resize_drive

// ---------------------------------------------------------------------------
// Native input bridge (see resize_drive.h). Content-space -> window base:
// AppKit's window base has its origin at the BOTTOM-left of the window frame;
// the contentView sits at contentView.frame within it. A content point (x, y)
// with y from the top is base (cv.origin.x + x, cv.origin.y + cv.height - y).
namespace {
NSPoint content_to_base(NSWindow* win, float cx, float cy) {
    const NSRect cv = [[win contentView] frame];
    return NSMakePoint(cv.origin.x + cx, cv.origin.y + cv.size.height - cy);
}
void post_native_mouse(NSEventType type, float cx, float cy) {
    NSWindow* win = the_window();
    if (!win) return;
    static int n = 1000;
    const NSPoint loc = content_to_base(win, cx, cy);
    NSEvent* e = [NSEvent mouseEventWithType:type
                                    location:loc
                               modifierFlags:0
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:[win windowNumber]
                                     context:nil
                                 eventNumber:++n
                                  clickCount:1
                                    pressure:(type == NSEventTypeLeftMouseUp ||
                                              type == NSEventTypeRightMouseUp)
                                                 ? 0.0f
                                                 : 1.0f];
    [NSApp postEvent:e atStart:NO];
}
}  // namespace

extern "C" void hanabi_native_activate(void) {
    NSWindow* win = the_window();
    [NSApp activateIgnoringOtherApps:YES];
    if (win) [win makeKeyAndOrderFront:nil];
    if (const char* v = getenv("HANABI_DBG_NATIVE"); v && *v && *v != '0')
        fprintf(stderr,
                "[DBG native] active=%d keyWindow=%d main=%d policy=%ld "
                "firstResponder=%s\n",
                (int)[NSApp isActive], (int)([NSApp keyWindow] != nil),
                (int)([NSApp mainWindow] != nil),
                (long)[NSApp activationPolicy],
                win ? [[[win firstResponder] className] UTF8String] : "-");
}

extern "C" void hanabi_native_mouse_move(float cx, float cy) {
    post_native_mouse(NSEventTypeMouseMoved, cx, cy);
}
extern "C" void hanabi_native_mouse_down(float cx, float cy, int right) {
    post_native_mouse(right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown, cx, cy);
}
extern "C" void hanabi_native_mouse_up(float cx, float cy, int right) {
    post_native_mouse(right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp, cx, cy);
}
extern "C" void hanabi_native_content_size(float* w, float* h) {
    NSWindow* win = the_window();
    if (!win) { if (w) *w = 0; if (h) *h = 0; return; }
    const NSSize cs = [[win contentView] frame].size;
    if (w) *w = static_cast<float>(cs.width);
    if (h) *h = static_cast<float>(cs.height);
}
extern "C" void hanabi_native_drag_resize(int dw, int dh, int steps) {
    NSWindow* win = the_window();
    if (!win || steps < 1) return;
    const NSRect wf = [win frame];
    // Three points inside the frame's bottom-right corner: the theme frame's
    // resize zone. Window base coordinates, as post_mouse (the driver) uses.
    NSPoint corner = NSMakePoint(NSMaxX(wf) - 3.0, NSMinY(wf) + 3.0);
    static int n = 5000;
    const auto post = [&](NSEventType t, NSPoint screen) {
        const NSRect f = [win frame];
        NSEvent* e = [NSEvent mouseEventWithType:t
                                        location:NSMakePoint(screen.x - f.origin.x, screen.y - f.origin.y)
                                   modifierFlags:0
                                       timestamp:[[NSProcessInfo processInfo] systemUptime]
                                    windowNumber:[win windowNumber]
                                         context:nil
                                     eventNumber:++n
                                      clickCount:1
                                        pressure:t == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
        [NSApp postEvent:e atStart:NO];
    };
    post(NSEventTypeLeftMouseDown, corner);
    for (int i = 1; i <= steps; ++i) {
        const double f = static_cast<double>(i) / steps;
        post(NSEventTypeLeftMouseDragged,
             NSMakePoint(corner.x + dw * f, corner.y - dh * f));
    }
    // The final position twice more, then the up: the tracking loop sizes
    // from the last drag it processed and a mouse-up moves nothing.
    post(NSEventTypeLeftMouseDragged, NSMakePoint(corner.x + dw, corner.y - dh));
    post(NSEventTypeLeftMouseDragged, NSMakePoint(corner.x + dw, corner.y - dh));
    post(NSEventTypeLeftMouseUp, NSMakePoint(corner.x + dw, corner.y - dh));
}
extern "C" int hanabi_native_has_window(void) { return the_window() != nil; }

namespace {
struct Mod { unsigned bit; unsigned short code; NSEventModifierFlags flag; };
constexpr Mod kMods[] = {{1u, 56, NSEventModifierFlagShift},     // kVK_Shift
                         {2u, 59, NSEventModifierFlagControl},   // kVK_Control
                         {4u, 58, NSEventModifierFlagOption},    // kVK_Option
                         {8u, 55, NSEventModifierFlagCommand}};  // kVK_Command
NSEventModifierFlags g_heldMods = 0;
void post_flags_changed(NSWindow* win, unsigned short code,
                        NSEventModifierFlags f) {
    NSEvent* e = [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                                  location:NSZeroPoint
                             modifierFlags:f
                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                              windowNumber:[win windowNumber]
                                   context:nil
                                characters:@""
               charactersIgnoringModifiers:@""
                                 isARepeat:NO
                                   keyCode:code];
    [NSApp postEvent:e atStart:NO];
}
}  // namespace

extern "C" void hanabi_native_mods_down(unsigned mods) {
    NSWindow* win = the_window();
    if (!win) return;
    for (const Mod& m : kMods)
        if (mods & m.bit) {
            g_heldMods |= m.flag;
            post_flags_changed(win, m.code, g_heldMods);
        }
}

extern "C" void hanabi_native_mods_up(unsigned mods) {
    NSWindow* win = the_window();
    if (!win) return;
    for (int i = 3; i >= 0; --i) {
        const Mod& m = kMods[i];
        if (!(mods & m.bit)) continue;
        g_heldMods &= ~m.flag;
        post_flags_changed(win, m.code, g_heldMods);
    }
}

extern "C" void hanabi_native_key(unsigned short key_code, const char* chars,
                                  unsigned /*mods*/) {
    NSWindow* win = the_window();
    if (!win) return;
    NSString* c = chars ? [NSString stringWithUTF8String:chars] : @"";
    // The modifiers are held by the caller across frames
    // (hanabi_native_mods_down / _up); the flags on this key event carry them
    // for AppKit's own key-equivalent matching.
    NSEventModifierFlags flags = g_heldMods;
    for (NSEventType t : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent* e = [NSEvent keyEventWithType:t
                                      location:NSZeroPoint
                                 modifierFlags:flags
                                     timestamp:[[NSProcessInfo processInfo] systemUptime]
                                  windowNumber:[win windowNumber]
                                       context:nil
                                    characters:c
                   charactersIgnoringModifiers:c
                                     isARepeat:NO
                                       keyCode:key_code];
        [NSApp postEvent:e atStart:NO];
    }
}
