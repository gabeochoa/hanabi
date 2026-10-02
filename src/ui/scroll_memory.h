#pragma once

// A list's place, kept across a trip away (afterhours_gaps.md #163).
//
// The library measures a scroll view that was not built this frame against
// zero children and clamps its offset to 0, so one frame on another screen
// sends a list back to the top. A screen that owns one of these calls step()
// once per build with the build-frame counter, a key for WHICH list it is
// showing (Home is one key; each digest view its own, sharing one scroll
// entity), and the offset it reads; while the screen stays put the offset is
// remembered, and for a few builds after a return or a switch of key the
// remembered offset (0 for a list never scrolled) is handed back to write --
// by then the rebuilt rows have measured and the offset survives the clamp.
//
// Pure: no UI types, so the rule is unit-tested on its own.

#include <climits>
#include <cstddef>
#include <optional>
#include <unordered_map>

namespace hanabi::ui {

struct ScrollMemory {
    static constexpr int kRestoreBuilds = 4;

    std::size_t lastFrame = 0;
    int lastKey = INT_MIN;
    int restoreLeft = 0;
    float restoreY = 0.0f;
    std::unordered_map<int, float> saved;

    // The offset to write this build, or nothing (leave the reader's own).
    std::optional<float> step(std::size_t frame, int key, float offset) {
        const bool returning = lastFrame != 0 && frame > lastFrame + 1;
        const bool switched = lastKey != INT_MIN && key != lastKey;
        lastFrame = frame;
        lastKey = key;
        if (returning || switched) {
            const auto it = saved.find(key);
            restoreY = it == saved.end() ? 0.0f : it->second;
            // A return to a list at its top needs no help; a switch does
            // (the shared entity still holds the other list's offset).
            restoreLeft = (switched || restoreY > 0.0f) ? kRestoreBuilds : 0;
        }
        if (restoreLeft > 0) {
            --restoreLeft;
            return restoreY;
        }
        saved[key] = offset;
        return std::nullopt;
    }
};

}  // namespace hanabi::ui
