#pragma once
// Named workspaces (the reference's Station layout, kt-4m8b): each workspace
// keeps its own open chats; the bar is a row of workspace chips, and under
// the Station layout a second row lists every chat once (session chips).
// Pure and graphics-free, like pane_layout.h. Hanabi has panes/tabs and
// Cmd+1-9 tab selection already; workspaces are the genuinely missing piece,
// so this model lands first and the bar/switch wiring follows it.
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace ecs::model {

struct Workspace {
    std::string name;
    std::vector<std::string> sessionIds;  // open chats, bar order
};

struct SessionChip {
    std::string sessionId;
    std::string title;
    int workspace = -1;
    bool isSelected = false;
};

// Title lookup is the caller's (the tab model owns titles); chips keep the
// workspace each chat is listed under so a click can reveal it.
struct WorkspaceSwitcher {
    std::vector<Workspace> workspaces{Workspace{"Workspace", {}}};
    int activeIndex = 0;

    int count() const { return static_cast<int>(workspaces.size()); }
    bool valid(int i) const { return i >= 0 && i < count(); }

    void select(int i) {
        if (valid(i)) activeIndex = i;
    }
    int add() {
        workspaces.push_back(Workspace{"Workspace " + std::to_string(count() + 1), {}});
        activeIndex = count() - 1;
        return activeIndex;
    }
    void rename(int i, std::string name) {
        if (valid(i) && !name.empty()) workspaces[static_cast<std::size_t>(i)].name = std::move(name);
    }
    // The reference's Duplicate: "<name> copy" inserted after the original,
    // sharing its chats; the copy becomes active.
    int duplicate(int i) {
        if (!valid(i)) return activeIndex;
        Workspace copy = workspaces[static_cast<std::size_t>(i)];
        copy.name += " copy";
        workspaces.insert(workspaces.begin() + i + 1, copy);
        activeIndex = i + 1;
        return activeIndex;
    }
    // Never closes the last workspace (the reference offers no close then).
    bool canClose(int i) const { return valid(i) && count() > 1; }
    void close(int i) {
        if (!canClose(i)) return;
        workspaces.erase(workspaces.begin() + i);
        if (activeIndex > i) --activeIndex;
        if (activeIndex >= count()) activeIndex = count() - 1;
    }
    void closeOthers(int i) {
        if (!valid(i)) return;
        Workspace keep = workspaces[static_cast<std::size_t>(i)];
        workspaces = {keep};
        activeIndex = 0;
    }
    void openChat(int i, const std::string& sessionId) {
        if (!valid(i)) return;
        auto& ids = workspaces[static_cast<std::size_t>(i)].sessionIds;
        if (std::find(ids.begin(), ids.end(), sessionId) == ids.end()) ids.push_back(sessionId);
    }

    // Every chat once, workspace by workspace. A chat shared by duplicates
    // is listed under the ACTIVE workspace's copy (the reference's rule).
    // Titles are the session ids here; the tab model maps id -> title at
    // draw time, this model only decides membership, order and selection.
    std::vector<SessionChip> sessionChips(bool station, const std::string& selectedId) const {
        if (!station) return {};
        std::vector<SessionChip> out;
        auto emit = [&](int w) {
            for (const auto& id : workspaces[static_cast<std::size_t>(w)].sessionIds) {
                auto dup = std::find_if(out.begin(), out.end(),
                                        [&](const SessionChip& c) { return c.sessionId == id; });
                if (dup != out.end()) continue;
                out.push_back(SessionChip{id, id, w, id == selectedId});
            }
        };
        // Active workspace first so its copies win the dedupe, then the rest
        // in bar order; stable partition below restores bar order.
        emit(activeIndex);
        for (int w = 0; w < count(); ++w)
            if (w != activeIndex) emit(w);
        std::stable_sort(out.begin(), out.end(),
                         [](const SessionChip& a, const SessionChip& b) { return a.workspace < b.workspace; });
        return out;
    }
};

// The chip's Cmd chord hint, only under the Station layout, only 1-9.
inline std::string chip_hint(int index, bool station) {
    if (!station || index < 0 || index > 8) return "";
    return std::string("\xE2\x8C\x98") + std::to_string(index + 1);  // ⌘n
}
inline std::string chip_help(const std::string& name, int index, bool station) {
    const std::string hint = chip_hint(index, true);
    if (!station) return hint;
    return hint.empty() ? name : name + "  " + hint;
}

}  // namespace ecs::model
