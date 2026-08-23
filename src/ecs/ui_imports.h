#pragma once

// Shared UI type aliases for ECS system headers. Include at file scope.

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/presets.h"
#include "../ui/theme.h"
#include "../util/scroll_prefs.h"
#include "../ui_context.h"
#include "components.h"

using afterhours::Entity;
using afterhours::EntityHelper;
using afterhours::EntityQuery;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::FontSize;
using afterhours::ui::FlexWrap;
using afterhours::ui::Overflow;
using afterhours::ui::TextOverflow;
using afterhours::ui::Axis;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::resolve_to_pixels;

namespace ecs {

// Find the first singleton component of type T (optionally requiring more).
template <typename T, typename... Filters>
inline T* find_singleton() {
    auto q = EntityQuery({.force_merge = true}).whereHasComponent<T>();
    (q.template whereHasComponent<Filters>(), ...);
    auto results = q.gen();
    if (results.empty()) return nullptr;
    return &results[0].get().template get<T>();
}

// Find the first entity carrying component T (optionally plus filters).
template <typename T, typename... Filters>
inline Entity* find_singleton_entity() {
    auto q = EntityQuery({.force_merge = true}).whereHasComponent<T>();
    (q.template whereHasComponent<Filters>(), ...);
    auto results = q.gen();
    if (results.empty()) return nullptr;
    return &results[0].get();
}

}  // namespace ecs
