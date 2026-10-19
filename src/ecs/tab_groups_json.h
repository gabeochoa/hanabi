#pragma once

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"
#include "tab_groups.h"

namespace ecs::model {

inline const char* axis_name(SplitAxis axis) {
    return axis == SplitAxis::Vertical ? "vertical" : "horizontal";
}

inline nlohmann::json tab_groups_to_json(const TabGroups& groups) {
    using nlohmann::json;
    json out = json::object();
    json list = json::array();
    for (int g = 0; g < groups.group_count(); ++g) {
        const auto tabs = groups.tabs_in(g);
        if (tabs.empty()) continue;
        json entry = json::object();
        json ids = json::array();
        json kept = json::array();
        for (const auto& t : tabs) {
            if (t.preview) continue;
            ids.push_back(t.id);
            if (t.kept) kept.push_back(t.id);
        }
        if (ids.empty()) continue;
        entry["tabs"] = std::move(ids);
        entry["kept"] = std::move(kept);
        if (const auto sel = groups.selected_in(g)) entry["selected"] = *sel;
        list.push_back(std::move(entry));
    }
    out["groups"] = std::move(list);
    if (groups.selected()) out["active"] = *groups.selected();
    if (const auto axis = TabGroups::persistable_axis(static_cast<int>(out["groups"].size()), groups.axis()))
        out["axis"] = axis_name(*axis);
    return out;
}

inline nlohmann::json legacy_split_layout(const std::vector<std::string>& openTabs,
                                          const std::string& left, const std::string& right,
                                          int focusedPane) {
    using nlohmann::json;
    if (left.empty() || right.empty() || left == right) return json();
    if (std::find(openTabs.begin(), openTabs.end(), right) == openTabs.end()) return json();
    json firstTabs = json::array();
    for (const std::string& id : openTabs)
        if (id != right) firstTabs.push_back(id);
    if (firstTabs.empty()) return json();
    const bool leftOpen = std::find(openTabs.begin(), openTabs.end(), left) != openTabs.end();
    const std::string shown = leftOpen ? left : firstTabs.front().get<std::string>();
    json first = {{"tabs", firstTabs}, {"kept", json::array()}, {"selected", shown}};
    json second = {{"tabs", json::array({right})}, {"kept", json::array()}, {"selected", right}};
    return json{{"groups", json::array({first, second})},
                {"active", focusedPane == 1 ? right : shown},
                {"axis", "horizontal"}};
}

inline TabGroups tab_groups_from_json(
    const nlohmann::json& in,
    const std::function<bool(const std::string&)>& wanted = [](const std::string&) { return true; }) {
    TabGroups out;
    if (!in.is_object() || !in.contains("groups") || !in["groups"].is_array()) return out;
    std::set<std::string> seen;
    std::vector<std::string> shown;
    for (const auto& entry : in["groups"]) {
        if (!entry.is_object() || !entry.contains("tabs") || !entry["tabs"].is_array()) continue;
        std::set<std::string> kept;
        if (entry.contains("kept") && entry["kept"].is_array())
            for (const auto& k : entry["kept"])
                if (k.is_string()) kept.insert(k.get<std::string>());
        const int target = out.empty() ? 0 : out.group_count();
        std::vector<std::string> placed;
        for (const auto& item : entry["tabs"]) {
            if (!item.is_string()) continue;
            const std::string id = item.get<std::string>();
            if (id.empty() || !wanted(id) || !seen.insert(id).second) continue;
            out.open(id, false, out.empty() ? 0 : (placed.empty() ? target : *out.group_of(placed.front())));
            placed.push_back(id);
            if (kept.count(id)) out.set_kept(id, true);
        }
        if (placed.empty()) continue;
        std::string pick = placed.front();
        if (entry.contains("selected") && entry["selected"].is_string()) {
            const std::string want = entry["selected"].get<std::string>();
            for (const auto& id : placed)
                if (id == want) pick = want;
        }
        shown.push_back(pick);
    }
    for (const auto& id : shown) out.select(id);
    if (in.contains("active") && in["active"].is_string() && out.contains(in["active"].get<std::string>()))
        out.select(in["active"].get<std::string>());
    if (in.contains("axis") && in["axis"].is_string() && out.group_count() > 1) {
        const std::string name = in["axis"].get<std::string>();
        if (name == "vertical") out.adopt_axis(SplitAxis::Vertical);
        else if (name == "horizontal") out.adopt_axis(SplitAxis::Horizontal);
    }
    return out;
}

}  // namespace ecs::model
