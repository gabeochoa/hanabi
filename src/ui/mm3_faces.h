#pragma once

// The MM3 character's faces (the reference's MM3Face, kt-q769; puffin_gaps.md
// D51): a third icon set beside Normal, chosen in Settings > Appearance >
// Icons. Every face is the source art, not a redrawing: these rectangles are
// the path data of the MM3 Animation Library's SVGs, copied as numbers from
// the reference (which copied them from the library). Each face shares one
// box (5,23 120x81) so they line up in a column; the wave's raised hand
// reaches above it and is drawn anyway. Still faces: a sidebar of thousands
// of rows does not animate.

#include <array>
#include <cstddef>

namespace hanabi::mm3 {

struct Cell {
    float x, y, w, h;
};

enum class Face { Neutral, Blink, Wink, Wave, Tired, Angry, Smile };

inline constexpr float kBoxX = 5.0f, kBoxY = 23.0f, kBoxW = 120.0f, kBoxH = 81.0f;
// The box a ROW or TAB draws a face in (the reference's MM3Face.rowBox,
// D123162830 / kt-tgsx): tall and wide enough for every frame of every face
// and animation -- the wave's raised hand (y 6), tired's chin (105) -- so no
// face is cut off by its slot. Faces keep one scale and line up.
inline constexpr float kRowX = 5.0f, kRowY = 6.0f, kRowW = 123.0f, kRowH = 99.0f;

inline constexpr std::array<Cell, 11> kNeutral{{{26, 25, 28, 20}, {74, 25, 28, 20}, {16, 45, 10, 19}, {102, 45, 10, 19}, {6, 64, 10, 19}, {46, 64, 9, 20}, {74, 64, 9, 20}, {112, 64, 10, 19}, {16, 83, 10, 19}, {102, 83, 10, 19}, {54, 93, 20, 9}}};
inline constexpr std::array<Cell, 19> kBlink{{{26, 25, 28, 20}, {74, 25, 28, 20}, {16, 45, 10, 19}, {102, 45, 10, 19}, {6, 64, 10, 19}, {112, 64, 10, 19}, {43, 71, 3, 6}, {46, 71, 9, 1}, {55, 71, 2, 6}, {71, 71, 3, 6}, {74, 71, 9, 1}, {83, 71, 2, 6}, {46, 72, 9, 4}, {74, 72, 9, 4}, {46, 76, 9, 1}, {74, 76, 9, 1}, {16, 83, 10, 19}, {102, 83, 10, 19}, {54, 93, 20, 9}}};
inline constexpr std::array<Cell, 16> kWink{{{26, 26, 28, 19}, {74, 26, 28, 19}, {16, 45, 10, 19}, {102, 45, 10, 19}, {69, 55, 9, 19}, {47, 58, 5, 5}, {52, 61, 5, 9}, {57, 63, 5, 5}, {6, 64, 10, 20}, {112, 64, 10, 20}, {47, 68, 5, 5}, {16, 84, 10, 19}, {102, 84, 10, 19}, {40, 86, 10, 9}, {78, 86, 10, 9}, {50, 89, 28, 9}}};
inline constexpr std::array<Cell, 63> kWave{{{99, 7, 1, 23}, {100, 7, 1, 24}, {98, 8, 1, 21}, {101, 8, 1, 24}, {97, 9, 1, 20}, {102, 9, 1, 23}, {103, 10, 1, 23}, {104, 10, 1, 24}, {96, 11, 1, 17}, {105, 11, 1, 24}, {106, 11, 1, 25}, {95, 12, 1, 16}, {107, 12, 1, 24}, {94, 13, 1, 14}, {108, 13, 1, 24}, {93, 14, 1, 12}, {109, 14, 1, 24}, {110, 14, 1, 25}, {111, 15, 1, 44}, {92, 16, 1, 9}, {112, 16, 1, 22}, {113, 17, 1, 19}, {91, 18, 1, 6}, {114, 18, 1, 17}, {115, 18, 1, 15}, {90, 19, 1, 5}, {116, 19, 1, 13}, {89, 20, 1, 3}, {117, 20, 1, 11}, {118, 21, 1, 9}, {119, 21, 1, 7}, {120, 22, 1, 4}, {121, 23, 1, 2}, {26, 26, 28, 19}, {112, 39, 2, 20}, {114, 40, 1, 20}, {115, 40, 1, 40}, {116, 40, 1, 16}, {110, 41, 1, 18}, {117, 41, 1, 11}, {118, 41, 1, 9}, {119, 41, 1, 5}, {16, 45, 10, 19}, {69, 45, 9, 19}, {109, 46, 1, 12}, {50, 50, 9, 19}, {108, 50, 1, 8}, {107, 53, 1, 5}, {106, 56, 1, 2}, {116, 60, 1, 20}, {117, 61, 5, 19}, {122, 61, 1, 15}, {123, 61, 1, 7}, {124, 62, 1, 1}, {6, 64, 10, 19}, {114, 66, 1, 14}, {113, 72, 1, 7}, {112, 77, 1, 2}, {102, 79, 10, 19}, {45, 80, 9, 10}, {74, 80, 9, 10}, {16, 83, 10, 19}, {54, 83, 20, 10}}};
inline constexpr std::array<Cell, 15> kTired{{{26, 26, 28, 19}, {74, 26, 28, 19}, {16, 45, 10, 19}, {102, 45, 10, 19}, {6, 64, 10, 19}, {112, 64, 10, 19}, {40, 80, 19, 5}, {69, 80, 19, 5}, {16, 83, 10, 19}, {102, 83, 10, 19}, {55, 94, 18, 3}, {55, 97, 1, 1}, {56, 97, 16, 1}, {72, 97, 1, 1}, {55, 98, 18, 4}}};
inline constexpr std::array<Cell, 22> kAngry{{{24, 26, 29, 19}, {72, 26, 29, 19}, {14, 45, 10, 19}, {101, 45, 10, 19}, {40, 57, 5, 2}, {78, 57, 5, 2}, {40, 59, 10, 3}, {74, 59, 9, 3}, {45, 62, 9, 4}, {69, 62, 9, 4}, {5, 64, 10, 19}, {110, 64, 10, 19}, {40, 66, 10, 3}, {74, 66, 9, 3}, {40, 69, 5, 2}, {78, 69, 5, 2}, {52, 75, 19, 4}, {47, 79, 10, 20}, {66, 79, 10, 20}, {14, 83, 10, 19}, {101, 83, 10, 19}, {52, 99, 19, 4}}};

inline constexpr std::array<Cell, 13> kSmile{{{26, 26, 28, 19}, {74, 26, 28, 19}, {16, 45, 10, 19}, {102, 45, 10, 19}, {50, 54, 9, 20}, {69, 54, 9, 20}, {6, 64, 10, 19}, {112, 64, 10, 19}, {16, 83, 10, 19}, {102, 83, 10, 19}, {45, 85, 9, 10}, {74, 85, 9, 10}, {54, 88, 20, 10}}};

struct Cells {
    const Cell* data;
    std::size_t size;
};

inline Cells cells(Face f) {
    switch (f) {
        case Face::Neutral: return {kNeutral.data(), kNeutral.size()};
        case Face::Blink: return {kBlink.data(), kBlink.size()};
        case Face::Wink: return {kWink.data(), kWink.size()};
        case Face::Wave: return {kWave.data(), kWave.size()};
        case Face::Tired: return {kTired.data(), kTired.size()};
        case Face::Angry: return {kAngry.data(), kAngry.size()};
        case Face::Smile: return {kSmile.data(), kSmile.size()};
    }
    return {kNeutral.data(), kNeutral.size()};
}

}  // namespace hanabi::mm3
