#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <vector>

#include "snooze_presets.h"

namespace hanabi::snooze_menu {

struct Leaf {
    std::string label;
    std::string debug_name;
    bool disabled = false;
    std::string action_id;
    bool separator = false;
};

struct Row {
    std::string label;
    std::string debug_name;
    bool disabled = false;
    std::string action_id;
    std::vector<Leaf> children;
};

inline constexpr const char* kSetPrefix = "snooze:";
inline constexpr const char* kClearId = "unsnooze";
inline constexpr const char* kParentId = "snooze";
inline constexpr const char* kCustomId = "snooze_custom";

struct Pick {
    std::optional<std::int64_t> until;
};

inline std::string leaf_id(std::int64_t until) { return std::string(kSetPrefix) + std::to_string(until); }

inline std::optional<Pick> parse(std::string_view action_id) {
    if (action_id == kClearId) return Pick{std::nullopt};
    const std::string_view prefix = kSetPrefix;
    if (action_id.size() <= prefix.size() || action_id.substr(0, prefix.size()) != prefix) return std::nullopt;
    const std::string digits(action_id.substr(prefix.size()));
    if (digits.empty()) return std::nullopt;
    for (char c : digits)
        if (c < '0' || c > '9') return std::nullopt;
    return Pick{static_cast<std::int64_t>(std::strtoll(digits.c_str(), nullptr, 10))};
}

inline Row item(std::optional<std::int64_t> snoozed_until, std::int64_t now, const char* debug_name,
                bool disabled, bool custom_available = false,
                const snooze_presets::LocalCalendar& cal = {}) {
    Row m;
    m.debug_name = debug_name;
    m.disabled = disabled;
    m.action_id = kParentId;
    if (snoozed_until) {
        m.label = snooze_presets::snoozed_until_text(*snoozed_until, now, cal);
        m.action_id = kClearId;
        return m;
    }
    m.label = "Snooze";
    for (const snooze_presets::Option& o : snooze_presets::options(now, cal)) {
        Leaf leaf;
        leaf.label = snooze_presets::option_text(o, now, cal);
        leaf.debug_name = std::string(debug_name) + "_" + snooze_presets::raw_name(o.choice);
        leaf.disabled = disabled;
        leaf.action_id = leaf_id(o.until);
        m.children.push_back(std::move(leaf));
    }
    if (custom_available && !m.children.empty()) {
        Leaf sep;
        sep.debug_name = std::string(debug_name) + "_divider";
        sep.disabled = true;
        sep.separator = true;
        m.children.push_back(std::move(sep));
        Leaf custom;
        custom.label = "Custom\xe2\x80\xa6";
        custom.debug_name = std::string(debug_name) + "_custom";
        custom.disabled = disabled;
        custom.action_id = kCustomId;
        m.children.push_back(std::move(custom));
    }
    return m;
}

template <class MenuItemT, class MenuLeafT>
MenuItemT to_menu_item(const Row& row) {
    MenuItemT m;
    m.label = row.label;
    m.debug_name = row.debug_name;
    m.disabled = row.disabled;
    m.action_id = row.action_id;
    for (const Leaf& l : row.children) {
        MenuLeafT leaf;
        leaf.label = l.label;
        leaf.debug_name = l.debug_name;
        leaf.disabled = l.disabled;
        leaf.action_id = l.action_id;
        leaf.separator = l.separator;
        m.children.push_back(std::move(leaf));
    }
    return m;
}

}
