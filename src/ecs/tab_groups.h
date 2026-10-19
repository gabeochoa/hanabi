#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ecs::model {

enum class SplitAxis { Horizontal, Vertical };

inline constexpr float kPaneMinWidth = 480.0f;
inline constexpr float kPaneGap = 1.0f;

struct GroupTab {
    std::string id;
    int group = 0;
    bool preview = false;
    bool kept = false;
};

class TabGroups {
  public:
    const std::vector<GroupTab>& tabs() const { return tabs_; }
    bool empty() const { return tabs_.empty(); }
    std::size_t size() const { return tabs_.size(); }
    SplitAxis axis() const { return axis_; }
    const std::vector<int>& group_keys() const { return keys_; }
    const std::optional<std::string>& selected() const { return selected_; }
    int acted_group() const { return acted_; }

    int group_count() const {
        int top = 0;
        for (const auto& t : tabs_) top = std::max(top, t.group);
        return top + 1;
    }

    int occupied_group_count() const {
        std::set<int> used;
        for (const auto& t : tabs_) used.insert(t.group);
        return static_cast<int>(used.size());
    }

    bool contains(const std::string& id) const { return index_of(id) >= 0; }

    int index_of(const std::string& id) const {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const GroupTab* find(const std::string& id) const {
        const int i = index_of(id);
        return i < 0 ? nullptr : &tabs_[static_cast<std::size_t>(i)];
    }

    std::vector<GroupTab> tabs_in(int group) const {
        std::vector<GroupTab> out;
        for (const auto& t : tabs_)
            if (t.group == group) out.push_back(t);
        return out;
    }

    std::optional<std::string> selected_in(int group) const {
        auto it = group_selection_.find(group);
        if (it == group_selection_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<std::string> previous_in(int group) const {
        auto it = previous_.find(group);
        if (it == previous_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<int> group_of(const std::string& id) const {
        const GroupTab* t = find(id);
        if (!t) return std::nullopt;
        return t->group;
    }

    void adopt_axis(SplitAxis axis) { axis_ = axis; }

    static std::optional<SplitAxis> persistable_axis(int groups, SplitAxis axis) {
        if (groups > 1) return axis;
        return std::nullopt;
    }

    const GroupTab& open(const std::string& id, bool preview = false,
                         std::optional<int> group = std::nullopt) {
        const int existing = index_of(id);
        if (existing >= 0) {
            if (!preview) tabs_[static_cast<std::size_t>(existing)].preview = false;
            select(id);
            return tabs_[static_cast<std::size_t>(index_of(id))];
        }
        int target = group.value_or(acted_);
        target = std::clamp(target, 0, group_count());
        if (tabs_.empty()) target = 0;
        if (preview) {
            for (auto& t : tabs_) {
                if (!t.preview) continue;
                const std::string old = t.id;
                const int g = t.group;
                t.id = id;
                if (group_selection_[g] == old) group_selection_[g] = id;
                forget_id(old);
                select(id);
                return tabs_[static_cast<std::size_t>(index_of(id))];
            }
        }
        GroupTab tab;
        tab.id = id;
        tab.group = target;
        tab.preview = preview;
        tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(insertion_point(target, false)),
                     std::move(tab));
        rekey();
        select(id);
        return tabs_[static_cast<std::size_t>(index_of(id))];
    }

    void set_kept(const std::string& id, bool kept) {
        const int idx = index_of(id);
        if (idx < 0 || tabs_[static_cast<std::size_t>(idx)].kept == kept) return;
        GroupTab tab = tabs_[static_cast<std::size_t>(idx)];
        tabs_.erase(tabs_.begin() + idx);
        tab.kept = kept;
        if (kept) tab.preview = false;
        const std::size_t at = insertion_point(tab.group, true);
        tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(at), std::move(tab));
    }

    void keep(const std::string& id) {
        const int i = index_of(id);
        if (i >= 0) tabs_[static_cast<std::size_t>(i)].preview = false;
    }

    bool select(const std::string& id) {
        const GroupTab* t = find(id);
        if (!t) return false;
        const int g = t->group;
        selected_ = id;
        auto sel = group_selection_.find(g);
        if (sel != group_selection_.end() && sel->second != id) previous_[g] = sel->second;
        group_selection_[g] = id;
        acted_ = g;
        return true;
    }

    void close(const std::string& id) {
        const int idx = index_of(id);
        if (idx < 0) return;
        const bool was_selected = selected_ && *selected_ == id;
        const int g = tabs_[static_cast<std::size_t>(idx)].group;
        int pos = 0;
        for (int i = 0; i < idx; ++i)
            if (tabs_[static_cast<std::size_t>(i)].group == g) ++pos;
        tabs_.erase(tabs_.begin() + idx);
        for (auto it = previous_.begin(); it != previous_.end();)
            it = it->second == id ? previous_.erase(it) : std::next(it);
        auto sel = group_selection_.find(g);
        if (sel != group_selection_.end() && sel->second == id) {
            const auto remaining = tabs_in(g);
            if (remaining.empty()) {
                group_selection_.erase(g);
            } else {
                const std::string positional =
                    remaining[std::min<std::size_t>(static_cast<std::size_t>(pos),
                                                    remaining.size() - 1)]
                        .id;
                std::string pick = positional;
                auto prev = previous_.find(g);
                if (prev != previous_.end())
                    for (const auto& r : remaining)
                        if (r.id == prev->second) pick = prev->second;
                group_selection_[g] = pick;
            }
        }
        const std::optional<std::string> intended = selected_in(g);
        normalize();
        if (!was_selected) return;
        selected_.reset();
        if (intended && contains(*intended)) {
            select(*intended);
            return;
        }
        if (auto near = shown_nearest(g)) {
            select(*near);
            return;
        }
        if (!tabs_.empty()) select(tabs_.front().id);
    }

    bool split_off(const std::string& id, int index, SplitAxis axis) {
        const int idx = index_of(id);
        if (idx < 0) return false;
        const int from = tabs_[static_cast<std::size_t>(idx)].group;
        if (!(tabs_in(from).size() > 1 || (index != from && index != from + 1))) return false;
        axis_ = axis;
        GroupTab tab = tabs_[static_cast<std::size_t>(idx)];
        tabs_.erase(tabs_.begin() + idx);
        auto sel = group_selection_.find(from);
        if (sel != group_selection_.end() && sel->second == id) {
            const auto rest = tabs_in(from);
            if (rest.empty()) group_selection_.erase(from);
            else group_selection_[from] = rest.front().id;
        }
        shift_from(index);
        tab.group = index;
        tab.preview = false;
        std::size_t at = tabs_.size();
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].group > index) {
                at = i;
                break;
            }
        tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(at), std::move(tab));
        normalize();
        select(id);
        return true;
    }

    int reserve_group(int index, SplitAxis axis) {
        axis_ = axis;
        shift_from(index);
        return index;
    }

    void move_to_group(const std::string& id, int group) {
        const int idx = index_of(id);
        if (group < 0 || group > group_count() || idx < 0 ||
            tabs_[static_cast<std::size_t>(idx)].group == group)
            return;
        GroupTab tab = tabs_[static_cast<std::size_t>(idx)];
        tabs_.erase(tabs_.begin() + idx);
        const int was_in = tab.group;
        tab.group = group;
        tab.preview = false;
        const std::size_t at = insertion_point(group, tab.kept);
        tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(at), std::move(tab));
        auto sel = group_selection_.find(was_in);
        if (sel != group_selection_.end() && sel->second == id) {
            const auto rest = tabs_in(was_in);
            if (rest.empty()) group_selection_.erase(was_in);
            else group_selection_[was_in] = rest.front().id;
        }
        normalize();
        select(id);
    }

    void move(int from, int to) {
        if (from < 0 || from >= static_cast<int>(tabs_.size())) return;
        to = std::clamp(to, 0, static_cast<int>(tabs_.size()) - 1);
        if (from == to) return;
        GroupTab tab = tabs_[static_cast<std::size_t>(from)];
        tabs_.erase(tabs_.begin() + from);
        tabs_.insert(tabs_.begin() + to, std::move(tab));
    }

    std::optional<std::string> step(int offset, bool wrapping = false) const {
        if (!selected_) return std::nullopt;
        const GroupTab* cur = find(*selected_);
        if (!cur) return std::nullopt;
        const auto strip = tabs_in(cur->group);
        int at = -1;
        for (std::size_t i = 0; i < strip.size(); ++i)
            if (strip[i].id == *selected_) at = static_cast<int>(i);
        if (at < 0) return std::nullopt;
        const int n = static_cast<int>(strip.size());
        if (wrapping) return strip[static_cast<std::size_t>(((at + offset) % n + n) % n)].id;
        return strip[static_cast<std::size_t>(std::clamp(at + offset, 0, n - 1))].id;
    }

    std::optional<std::string> pane_step(int offset) const {
        const int count = group_count();
        if (count < 2) return std::nullopt;
        const int from = acted_;
        for (int hop = 1; hop <= count; ++hop) {
            const int g = ((from + offset * hop) % count + count) % count;
            if (g == from) break;
            if (auto s = selected_in(g)) return s;
        }
        return std::nullopt;
    }

    void normalize() {
        std::set<int> used;
        for (const auto& t : tabs_) used.insert(t.group);
        std::map<int, int> renumber;
        int position = 0;
        for (int g : used) renumber[g] = position++;
        std::vector<int> keys;
        for (const auto& [old, pos] : renumber) {
            (void)pos;
            keys.push_back(old >= 0 && old < static_cast<int>(keys_.size())
                               ? keys_[static_cast<std::size_t>(old)]
                               : mint());
        }
        keys_ = std::move(keys);
        for (auto& t : tabs_) t.group = renumber[t.group];
        group_selection_ = remap(group_selection_, renumber);
        previous_ = remap(previous_, renumber);
        auto a = renumber.find(acted_);
        acted_ = a == renumber.end() ? 0 : a->second;
        rekey();
    }

  private:
    using SelectionMap = std::map<int, std::string>;

    static SelectionMap remap(const SelectionMap& in, const std::map<int, int>& renumber) {
        SelectionMap out;
        for (const auto& [g, id] : in) {
            auto it = renumber.find(g);
            if (it != renumber.end()) out[it->second] = id;
        }
        return out;
    }

    int mint() { return next_key_++; }

    void rekey() {
        const std::size_t count = static_cast<std::size_t>(group_count());
        if (keys_.size() > count) keys_.resize(count);
        while (keys_.size() < count) keys_.push_back(mint());
    }

    void forget_id(const std::string& id) {
        for (auto it = previous_.begin(); it != previous_.end();)
            it = it->second == id ? previous_.erase(it) : std::next(it);
    }

    std::size_t insertion_point(int group, bool kept) const {
        std::size_t first = tabs_.size(), last = 0;
        bool any = false;
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].group != group) continue;
            if (!any) first = i;
            any = true;
            last = i;
        }
        if (!any) {
            for (std::size_t i = 0; i < tabs_.size(); ++i)
                if (tabs_[i].group > group) return i;
            return tabs_.size();
        }
        if (!kept) return last + 1;
        std::size_t at = first;
        while (at <= last && tabs_[at].kept) ++at;
        return at;
    }

    void shift_from(int index) {
        for (auto& t : tabs_)
            if (t.group >= index) ++t.group;
        auto shift = [&](SelectionMap& m) {
            SelectionMap out;
            for (const auto& [g, id] : m) out[g >= index ? g + 1 : g] = id;
            m = std::move(out);
        };
        shift(group_selection_);
        shift(previous_);
        if (acted_ >= index) ++acted_;
        const std::size_t at = static_cast<std::size_t>(
            std::clamp(index, 0, static_cast<int>(keys_.size())));
        keys_.insert(keys_.begin() + static_cast<std::ptrdiff_t>(at), mint());
        rekey();
    }

    std::optional<std::string> shown_nearest(int group) const {
        std::vector<int> order;
        for (int g = 0; g < group_count(); ++g) order.push_back(g);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const int da = std::abs(a - group), db = std::abs(b - group);
            return da != db ? da < db : a < b;
        });
        for (int g : order) {
            auto s = selected_in(g);
            if (s && contains(*s)) return s;
        }
        return std::nullopt;
    }

    std::vector<GroupTab> tabs_;
    SelectionMap group_selection_;
    SelectionMap previous_;
    std::vector<int> keys_;
    int next_key_ = 0;
    std::optional<std::string> selected_;
    int acted_ = 0;
    SplitAxis axis_ = SplitAxis::Horizontal;
};

inline std::vector<float> pane_extents(float total, int panes, float gap = kPaneGap,
                                       float floor = kPaneMinWidth) {
    if (panes <= 0) return {};
    const float share = (total - gap * static_cast<float>(panes - 1)) / static_cast<float>(panes);
    return std::vector<float>(static_cast<std::size_t>(panes), std::max(share, floor));
}

inline float required_extent(int panes, float gap = kPaneGap, float floor = kPaneMinWidth) {
    if (panes <= 0) return 0.0f;
    return floor * static_cast<float>(panes) + gap * static_cast<float>(panes - 1);
}

}  // namespace ecs::model
