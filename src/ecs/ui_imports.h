#pragma once

// Shared UI type aliases for ECS system headers. Include at file scope.

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/presets.h"
#include "../ui/theme.h"
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

namespace ecs {

// Find the first singleton component of type T (optionally requiring U too).
template <typename T>
inline T* find_singleton() {
    auto q = EntityQuery({.force_merge = true}).whereHasComponent<T>().gen();
    if (q.empty()) return nullptr;
    return &q[0].get().template get<T>();
}

}  // namespace ecs
