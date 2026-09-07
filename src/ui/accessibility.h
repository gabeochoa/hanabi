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

enum class Role {
    None,
    Button,
    MenuItem,
    Menu,
    Tooltip,
    Tab,
    Row,
    Field,
};

inline const char* role_name(Role r) {
    switch (r) {
        case Role::None: return "";
        case Role::Button: return "button";
        case Role::MenuItem: return "menuitem";
        case Role::Menu: return "menu";
        case Role::Tooltip: return "tooltip";
        case Role::Tab: return "tab";
        case Role::Row: return "row";
        case Role::Field: return "field";
    }
    return "";
}

struct AccessibleName : afterhours::BaseComponent {
    std::string value;
    Role role = Role::None;
    bool enabled = true;
    bool selected = false;
    bool has_submenu = false;
    bool expanded = false;
    std::string parent;
};

struct Semantics {
    std::string_view value;
    Role role = Role::None;
    bool enabled = true;
    bool selected = false;
    bool has_submenu = false;
    bool expanded = false;
    std::string_view parent;
};

inline void set_name(afterhours::Entity& entity, std::string_view value,
                     Role role = Role::Button) {
    auto& name = entity.addComponentIfMissing<AccessibleName>();
    if (name.value != value) name.value.assign(value);
    if (name.role == Role::None) name.role = role;
}

inline void describe(afterhours::Entity& entity, const Semantics& s) {
    auto& name = entity.addComponentIfMissing<AccessibleName>();
    if (name.value != s.value) name.value.assign(s.value);
    name.role = s.role;
    name.enabled = s.enabled;
    name.selected = s.selected;
    name.has_submenu = s.has_submenu;
    name.expanded = s.expanded;
    if (name.parent != s.parent) name.parent.assign(s.parent);
}

inline std::string spoken(const AccessibleName& n) {
    std::string out = n.value;
    if (n.role != Role::None) {
        out += ", ";
        out += role_name(n.role);
    }
    if (!n.enabled) out += ", dimmed";
    if (n.selected) out += ", selected";
    if (n.has_submenu) out += n.expanded ? ", submenu expanded" : ", submenu";
    return out;
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
