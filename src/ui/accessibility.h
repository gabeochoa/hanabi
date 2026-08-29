#pragma once

#include <string>
#include <string_view>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../../vendor/afterhours/src/core/entity.h"
#include "../../vendor/afterhours/src/plugins/ui/ui_core_components.h"

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
#include "../../vendor/afterhours/src/core/system.h"
#include "../../vendor/afterhours/src/plugins/e2e_testing/platform_test_input.h"
#endif

namespace hanabi::a11y {

struct AccessibleName : afterhours::BaseComponent {
    std::string value;
};

inline void set_name(afterhours::Entity& entity, std::string_view value) {
    auto& name = entity.addComponentIfMissing<AccessibleName>();
    if (name.value != value) name.value.assign(value);
}

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
struct RegisterAccessibleNames : afterhours::System<AccessibleName> {
    void for_each_with(afterhours::Entity& entity, AccessibleName& name,
                       float) override {
        if (!entity.has<afterhours::ui::UIComponent>() ||
            !entity.get<afterhours::ui::UIComponent>().was_rendered_to_screen)
            return;
        afterhours::testing::platform_input::register_visible_text(name.value);
    }
};
#endif

}  // namespace hanabi::a11y
