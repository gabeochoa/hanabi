#pragma once

// Hanabi color theme — a clean dark palette (VS Code / Bear-inspired).
// Only UI-chrome colors here; nothing domain-specific.

#include <afterhours/src/drawing_helpers.h>

#include <bitset>

namespace theme {

using Color = afterhours::Color;

// Window chrome
inline Color WINDOW_BG = {28, 28, 32, 255};    // app background
inline Color SIDEBAR_BG = {22, 22, 26, 255};   // session list
inline Color PANEL_BG = {28, 28, 32, 255};     // transcript
inline Color BORDER = {52, 52, 60, 255};

// Text
inline Color TEXT_PRIMARY = {222, 222, 228, 255};
inline Color TEXT_SECONDARY = {138, 138, 150, 255};
inline Color TEXT_ACCENT = {126, 176, 255, 255};
inline Color EMPTY_STATE_TEXT = {110, 110, 122, 255};

// Interactive
inline Color BUTTON_PRIMARY = {90, 128, 255, 255};
inline Color BUTTON_SECONDARY = {56, 56, 64, 255};
inline Color HOVER_BG = {40, 40, 48, 255};
inline Color SELECTED_BG = {48, 66, 120, 255};
inline Color ROW_SEPARATOR = {40, 40, 48, 255};
inline Color FOCUS_RING = {90, 128, 255, 255};

// States
inline Color DISABLED_BG = {44, 44, 50, 255};
inline Color DISABLED_TEXT = {120, 120, 128, 255};
inline Color STATUS_DELETED = {224, 88, 84, 255};  // used as "destructive"/error

// Message role accents (transcript)
inline Color ROLE_USER = {90, 128, 255, 255};       // blue
inline Color ROLE_ASSISTANT = {126, 200, 140, 255}; // green
inline Color ROLE_SYSTEM = {180, 150, 90, 255};     // amber
inline Color ROLE_TOOL = {150, 130, 200, 255};      // purple

inline Color BUBBLE_USER_BG = {40, 52, 84, 255};
inline Color BUBBLE_ASSISTANT_BG = {32, 40, 36, 255};
inline Color BUBBLE_OTHER_BG = {40, 40, 48, 255};

// Status pips for session list
inline Color STATUS_ACTIVE = {126, 200, 140, 255};
inline Color STATUS_IDLE = {180, 150, 90, 255};
inline Color STATUS_ARCHIVED = {110, 110, 122, 255};

}  // namespace theme

// Layout constants referenced by the design-system presets.
namespace theme {
namespace layout {

// Rounded corners (all four).
inline const std::bitset<4> ROUNDED_CORNERS = std::bitset<4>(0b1111);

// Roundness (0.0 = square, 1.0 = pill).
constexpr float ROUNDNESS_BUTTON = 0.4f;
constexpr float ROUNDNESS_BADGE = 0.6f;
constexpr float ROUNDNESS_BOX = 0.3f;

// Row sizing.
constexpr int FILE_ROW_HEIGHT = 24;

}  // namespace layout
}  // namespace theme
