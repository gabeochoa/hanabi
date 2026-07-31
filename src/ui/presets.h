#pragma once

#include <string>

#include "../../vendor/afterhours/src/plugins/ui/component_config.h"
#include "theme.h"

// Design system presets for hanabi.
// Each function returns a ComponentConfig with all design-system defaults baked in.
// Callers can chain .with_*() to override any property.

namespace preset {

using afterhours::Color;
using afterhours::ui::AlignItems;
using afterhours::ui::Axis;
using afterhours::ui::ComponentSize;
using afterhours::ui::FlexDirection;
using afterhours::ui::JustifyContent;
using afterhours::ui::Margin;
using afterhours::ui::Overflow;
using afterhours::ui::Padding;
using afterhours::ui::TextAlignment;
using afterhours::ui::children;
using afterhours::ui::h720;
using afterhours::ui::percent;
using afterhours::ui::pixels;
using afterhours::ui::w1280;
using afterhours::ui::FontSize;
using afterhours::ui::imm::ComponentConfig;

// ============================================================================
// Buttons
// ============================================================================

// Standard button. Default style is primary (blue bg, white text).
// Override bg/text for secondary (.with_custom_background(theme::BUTTON_SECONDARY))
// or destructive (.with_custom_background(theme::STATUS_DELETED)) variants.
//
// Default size: children() x h720(32). No explicit font size (uses framework default).
// Toolbar/tab buttons should chain .with_font_size() and .with_size() as needed.
inline ComponentConfig Button(const std::string& label, bool enabled = true) {
    auto bg = enabled ? theme::BUTTON_PRIMARY : theme::DISABLED_BG;
    auto text = enabled ? Color{255, 255, 255, 255} : theme::DISABLED_TEXT;
    auto config = ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{children(), h720(32)})
        .with_padding(Padding{
            .top = h720(0), .right = w1280(16),
            .bottom = h720(0), .left = w1280(16)})
        .with_custom_background(bg)
        .with_custom_text_color(text)
        .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
        .with_roundness(theme::layout::ROUNDNESS_BUTTON)
        .with_alignment(TextAlignment::Center);
    config.disabled = !enabled;
    return config;
}

// ============================================================================
// Section Headers
// ============================================================================

// Section header for sidebar groups ("Staged Changes 3", "Commits 42").
// Default size: percent(1.0f) x h720(24). Fixed height ensures consistent
// font rendering across all section headers.
inline ComponentConfig SectionHeader(const std::string& label) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{percent(1.0f), h720(22)})
        .with_padding(Padding{
            .top = h720(3), .right = pixels(8),
            .bottom = h720(3), .left = pixels(8)})
        .with_transparent_bg()
        .with_custom_text_color(Color{180, 180, 180, 255})
        .with_font_size(FontSize::Medium)
        .with_alignment(TextAlignment::Left)
        .with_roundness(0.0f);
}

// ============================================================================
// Selectable Rows
// ============================================================================

// Row container for selectable list items (files, commits, branches).
// Sets hover, cursor, flex direction, alignment, gap, and roundness.
// Default size: percent(1.0f) x h720(FILE_ROW_HEIGHT).
inline ComponentConfig SelectableRow(bool selected) {
    auto bg = selected ? theme::SELECTED_BG : theme::SIDEBAR_BG;
    auto hover = selected ? theme::SELECTED_BG : theme::HOVER_BG;
    return ComponentConfig{}
        .with_size(ComponentSize{percent(1.0f),
                                 h720(static_cast<float>(theme::layout::FILE_ROW_HEIGHT))})
        .with_flex_direction(FlexDirection::Row)
        .with_flex_wrap(afterhours::ui::FlexWrap::NoWrap)
        .with_align_items(AlignItems::Center)
        .with_custom_background(bg)
        .with_custom_hover_bg(hover)
        .with_cursor(afterhours::ui::CursorType::Pointer)
        .with_padding(Padding{
            .top = pixels(0), .right = pixels(4),
            .bottom = pixels(0), .left = pixels(8)})
        .with_gap(pixels(3))
        .with_roundness(0.0f);
}

// ============================================================================
// Badges
// ============================================================================

// Rounded pill badge for branch types, commit decorations, etc.
// Default size: children() x children().
inline ComponentConfig Badge(const std::string& label, Color bg, Color text) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{children(), children()})
        .with_padding(Padding{
            .top = pixels(1), .right = pixels(5),
            .bottom = pixels(1), .left = pixels(5)})
        .with_custom_background(bg)
        .with_custom_text_color(text)
        .with_font_size(h720(16))
        .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
        .with_roundness(theme::layout::ROUNDNESS_BADGE)
        .with_alignment(TextAlignment::Center);
}

// ============================================================================
// Text Presets
// ============================================================================

// Body text for primary content (file names, branch names, commit subjects).
// Default size: percent(1.0f) x children(). Left-aligned.
inline ComponentConfig BodyText(const std::string& label) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{percent(1.0f), children()})
        .with_transparent_bg()
        .with_custom_text_color(theme::TEXT_PRIMARY)
        .with_font_size(FontSize::Medium)
        .with_alignment(TextAlignment::Left)
        .with_roundness(0.0f);
}

// Secondary/metadata text (hashes, timestamps, tracking info).
// Default size: children() x children().
inline ComponentConfig MetaText(const std::string& label) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{children(), children()})
        .with_transparent_bg()
        .with_custom_text_color(theme::TEXT_SECONDARY)
        .with_font_size(h720(16))
        .with_roundness(0.0f);
}

// Empty state message ("No changes", "No branches found").
// Default size: percent(1.0f) x children(). Center-aligned.
inline ComponentConfig EmptyStateText(const std::string& label) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{percent(1.0f), children()})
        .with_custom_text_color(theme::EMPTY_STATE_TEXT)
        .with_alignment(TextAlignment::Center)
        .with_roundness(0.0f);
}

// Caption text for small labels and indicators.
// Default size: children() x children().
inline ComponentConfig CaptionText(const std::string& label) {
    return ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{children(), children()})
        .with_custom_text_color(theme::TEXT_SECONDARY)
        .with_font_size(h720(16))
        .with_roundness(0.0f);
}

// Dialog body message text.
// Default size: percent(1.0f) x children(). Left-aligned.
inline ComponentConfig DialogMessage(const std::string& text) {
    return ComponentConfig{}
        .with_label(text)
        .with_size(ComponentSize{percent(1.0f), children()})
        .with_padding(Padding{
            .top = h720(8), .right = w1280(16),
            .bottom = h720(8), .left = w1280(16)})
        .with_custom_text_color(theme::TEXT_PRIMARY)
        .with_alignment(TextAlignment::Left);
}

// ============================================================================
// Layout / Containers
// ============================================================================

// 1px horizontal separator between rows.
// Default size: percent(1.0f) x pixels(1).
inline ComponentConfig RowSeparator() {
    return ComponentConfig{}
        .with_size(ComponentSize{percent(1.0f), pixels(1)})
        .with_custom_background(theme::ROW_SEPARATOR)
        .with_roundness(0.0f);
}

// Scrollable panel (sidebar file list, commit log, refs list).
// Default size: percent(1.0f) x percent(1.0f). Overflow auto on Y axis.
inline ComponentConfig ScrollPanel() {
    return ComponentConfig{}
        .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
        .with_overflow(Overflow::Auto, Axis::Y)
        .with_flex_direction(FlexDirection::Column)
        .with_custom_background(theme::SIDEBAR_BG)
        .with_roundness(0.0f);
}

// Dialog button row (Cancel / OK / Delete).
// Default size: percent(1.0f) x h720(44). Flex-end justified.
inline ComponentConfig DialogButtonRow() {
    return ComponentConfig{}
        .with_size(ComponentSize{percent(1.0f), h720(44)})
        .with_flex_direction(FlexDirection::Row)
        .with_justify_content(JustifyContent::FlexEnd)
        .with_align_items(AlignItems::Center)
        .with_padding(Padding{
            .top = h720(8), .right = w1280(16),
            .bottom = h720(8), .left = w1280(16)});
}

} // namespace preset
