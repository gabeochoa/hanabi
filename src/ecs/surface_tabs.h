#pragma once

// A tab that is not a conversation.
//
// The tab strip keys everything off a session id string, which is exactly
// what a surface needs: a RESERVED id no thread can have. Settings opens as
// one of these -- a real tab, selected like any other, closed by its own ×,
// remembered in the strip's order -- rather than a sheet floating over the
// window. The reference's model is the same shape (a prefixed id per
// non-conversation surface, a title for the chip, and the id as the thing
// that makes it a singleton), and this file is the whole vocabulary: no
// other file spells the prefix.
//
// Pure and header-only so a unit test can hold the round trip.

#include <optional>
#include <string>
#include <string_view>

namespace ecs::model {

enum class Surface {
    Settings,
};

// The scheme is one a session id can never be: ids from the backend are
// opaque tokens, and nothing mints one with a colon and a slash.
inline constexpr std::string_view kSurfacePrefix = "hanabi:surface/";

[[nodiscard]] inline std::string_view surface_name(Surface surface) {
    switch (surface) {
        case Surface::Settings:
            return "settings";
    }
    return "";
}

// The chip's label. Not a thread title: a surface has no summary to read.
[[nodiscard]] inline std::string_view surface_title(Surface surface) {
    switch (surface) {
        case Surface::Settings:
            return "Settings";
    }
    return "";
}

[[nodiscard]] inline std::string surface_tab_id(Surface surface) {
    return std::string(kSurfacePrefix) + std::string(surface_name(surface));
}

// The id a tab carries, back to the surface it names -- or nothing, which is
// the ordinary case: a conversation.
[[nodiscard]] inline std::optional<Surface> surface_of(std::string_view id) {
    if (id.substr(0, kSurfacePrefix.size()) != kSurfacePrefix) return {};
    const std::string_view name = id.substr(kSurfacePrefix.size());
    if (name == surface_name(Surface::Settings)) return Surface::Settings;
    return {};
}

[[nodiscard]] inline bool is_surface_tab(std::string_view id) {
    return surface_of(id).has_value();
}

// Whether a pane showing this id is showing the settings surface.
[[nodiscard]] inline bool is_settings_tab(std::string_view id) {
    return surface_of(id) == Surface::Settings;
}

}  // namespace ecs::model
