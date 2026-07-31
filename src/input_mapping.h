#pragma once

enum class InputAction {
    None,
    // Required by afterhours UI systems
    WidgetUp,
    WidgetDown,
    WidgetRight,
    WidgetLeft,
    WidgetNext,
    WidgetPress,
    WidgetMod,
    WidgetBack,
    // Required by afterhours text_input (T031)
    TextBackspace,
    TextDelete,
    TextHome,
    TextEnd,
    TextSelectAll,
    MenuBack,
    // Will be extended by T040 (keyboard navigation)
};
