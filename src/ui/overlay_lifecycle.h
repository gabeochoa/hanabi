#pragma once

#include <vector>

#include "secondary_surface_geometry.h"

namespace hanabi::overlay {

inline bool inside(const surface::Rect& panel, float px, float py) {
    return px >= panel.x && px <= panel.x + panel.width && py >= panel.y &&
           py <= panel.y + panel.height;
}

inline bool dismisses(bool scrim_pressed, float px, float py,
                      const surface::Rect& panel) {
    return scrim_pressed && !inside(panel, px, py);
}

inline bool overlaps(const surface::Rect& a, const surface::Rect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

inline std::vector<surface::Rect>& occluders() {
    static std::vector<surface::Rect> value;
    return value;
}

inline void publish_occluder(const surface::Rect& r) {
    occluders().push_back(r);
}

inline void clear_occluders() { occluders().clear(); }

inline bool occluded(const surface::Rect& r) {
    for (const surface::Rect& o : occluders())
        if (overlaps(o, r)) return true;
    return false;
}

}  // namespace hanabi::overlay
