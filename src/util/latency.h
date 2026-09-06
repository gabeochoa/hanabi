#pragma once

// Interaction latency: frames from an accepted input to the first ink or
// state the interaction produced. docs/perf/LATENCY.md has the measurements,
// the budgets and why the unit is frames.
//
// The invariant a reader needs here: a watcher is ARMED only while its target
// is ABSENT, so an arm can never measure something that was already on screen.

#include <cstdio>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/plugins/ui/entity_management.h"

namespace hanabi::latency {

enum class Kind { Ui, Text, Focus };

struct Watch {
    std::string label;
    Kind kind = Kind::Ui;
    std::string target;
    bool pending = false;  // armed, still waiting for its input to land
    long armed = -1;       // frame that input landed
    long first_seen = -1;  // frame the target really appeared
    long settled = -1;     // frame the observation was accepted
};

inline std::vector<Watch>& watches() {
    static std::vector<Watch> v;
    return v;
}

inline long now_frame() {
    return static_cast<long>(afterhours::ui::imm::ui_build_frame);
}

inline Watch* find(const std::string& label) {
    for (auto& w : watches())
        if (w.label == label) return &w;
    return nullptr;
}

// Arm BEFORE the input. The clock starts when the input lands, not here, so
// the runner's gap between arming and acting is not counted.
inline void arm(const std::string& label, Kind kind, const std::string& target) {
    Watch fresh;
    fresh.label = label;
    fresh.kind = kind;
    fresh.target = target;
    fresh.pending = true;
    if (Watch* existing = find(label)) {
        *existing = fresh;
        return;
    }
    watches().push_back(fresh);
}

// An input landed this frame: every pending watcher starts counting from it.
inline void input_landed() {
    for (auto& w : watches())
        if (w.pending) {
            w.pending = false;
            w.armed = now_frame();
        }
}

// Frames from input to first appearance, or -1 if never armed or never seen.
inline long latency(const std::string& label) {
    const Watch* w = find(label);
    if (w == nullptr || w->armed < 0 || w->settled < 0) return -1;
    return w->settled - w->armed;
}

}  // namespace hanabi::latency
