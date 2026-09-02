#pragma once

#include <utility>

#include "../../vendor/afterhours/src/plugins/ui/imm_components.h"

namespace hanabi::ui {

inline constexpr struct DivFn {
    template <typename Ctx>
    afterhours::ui::imm::ElementResult operator()(
        Ctx& ctx, afterhours::ui::imm::EntityParent ep,
        afterhours::ui::imm::ComponentConfig& config) const {
        return afterhours::ui::imm::div(ctx, ep, std::move(config));
    }

    template <typename Ctx>
    afterhours::ui::imm::ElementResult operator()(
        Ctx& ctx, afterhours::ui::imm::EntityParent ep,
        afterhours::ui::imm::ComponentConfig&& config = {}) const {
        return afterhours::ui::imm::div(ctx, ep, std::move(config));
    }
} div{};

}  // namespace hanabi::ui
