#pragma once

#include "external.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang diagnostic ignored "-Wdeprecated-volatile"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdangling-reference"
#endif

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#define AFTER_HOURS_IMM_UI

#include <afterhours/ah.h>
#include <afterhours/src/developer.h>

namespace afterhours {
using vec2 = Vector2Type;
}

#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/plugins/window_manager.h>

#include <cassert>

typedef Vector2Type vec2;
struct vec3 {
    float x, y, z;
};
struct vec4 {
    float x, y, z, w;
};
using Rectangle = RectangleType;

#include <afterhours/src/plugins/autolayout.h>
#include <afterhours/src/plugins/ui.h>

using afterhours::ui::Spacing;
using afterhours::ui::imm::DefaultSpacing;

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
