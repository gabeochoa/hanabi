#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecs::model {

struct TranscriptItem {
    enum Kind {
        Bubble,
        ToolPile,
        ToolBlock,
        Spawn,
        NewDivider,
        DateDivider,
        Thinking,
        RunOutcome,
        Event,
        Delivery,
    } kind = Bubble;
    int lo = 0;
    int hi = 0;
    float height = 0.0f;
    bool isLive = false;
    bool showAuthor = true;
};

enum class TranscriptMutationKind {
    Reset,
    Append,
    Prepend,
    Update,
};

struct TranscriptMutation {
    std::uint64_t base_revision = 0;
    std::uint64_t revision = 0;
    TranscriptMutationKind kind = TranscriptMutationKind::Reset;
    std::size_t first = 0;
    std::size_t count = 0;
};

struct TranscriptGeometryFacts {
    float pane_width = 0.0f;
    bool show_date_dividers = false;
    bool show_reasoning = false;
    bool fold_long_messages = false;
    int tool_fold_mode = 0;
    int unread_first = -1;
    int unread_count = 0;
    std::uint64_t fold_revision = 0;
    int fold_dirty_index = -1;
    bool find_open = false;
    std::string find_query;
    bool streaming = false;
    std::size_t live_index = 0;
    int stream_phase = 0;
    unsigned font_epoch = 0;
};

class TranscriptItemIndex {
  public:
    static constexpr std::size_t kMaxSlots = 4;

    struct View {
        const std::vector<TranscriptItem>* items = nullptr;
        float height = 0.0f;
        std::size_t messages_visited = 0;
        bool rebuilt = false;
        bool full_rebuild = false;
    };

    template <class Build>
    View update(const std::string& key, const void* data_identity,
                std::size_t message_count, const TranscriptMutation& mutation,
                const TranscriptGeometryFacts& facts, Build&& build) {
        Slot& slot = touch(key);
        std::size_t restart = slot.dirty_from;
        bool full = slot.force_full || (slot.items.empty() && !slot.initialized);

        if (!full && restart == no_restart() &&
            mutation.revision == slot.revision &&
            data_identity == slot.data_identity && facts_equal(facts, slot.facts)) {
            return View{&slot.items, slot.height, 0, false, false};
        }

        if (!full) {
            if (!global_facts_equal(facts, slot.facts)) {
                full = true;
            } else {
                if (facts.unread_first != slot.facts.unread_first ||
                    facts.unread_count != slot.facts.unread_count) {
                    restart = earlier_boundary(facts.unread_first,
                                               slot.facts.unread_first);
                }
                if (facts.fold_revision != slot.facts.fold_revision) {
                    if (facts.fold_dirty_index < 0) full = true;
                    else restart = std::min(restart,
                                            static_cast<std::size_t>(
                                                facts.fold_dirty_index));
                }
                if (facts.streaming != slot.facts.streaming ||
                    facts.live_index != slot.facts.live_index ||
                    facts.stream_phase != slot.facts.stream_phase) {
                    if (facts.streaming)
                        restart = std::min(restart, facts.live_index);
                    if (slot.facts.streaming)
                        restart = std::min(restart, slot.facts.live_index);
                }
            }
        }

        if (!full && mutation.revision != slot.revision) {
            if (mutation.base_revision != slot.revision) {
                full = true;
            } else {
                switch (mutation.kind) {
                    case TranscriptMutationKind::Reset:
                    case TranscriptMutationKind::Prepend:
                        full = true;
                        break;
                    case TranscriptMutationKind::Append:
                    case TranscriptMutationKind::Update:
                        restart = std::min(restart,
                                           mutation.first == 0
                                               ? std::size_t{0}
                                               : mutation.first - 1);
                        break;
                }
            }
        } else if (!full && data_identity != slot.data_identity) {
            full = true;
        }

        if (!full && restart == no_restart()) full = true;
        if (full) restart = 0;
        restart = std::min(restart, message_count);
        restart = safe_restart(slot.items, restart);

        if (restart == 0) {
            slot.items.clear();
            slot.items.reserve(message_count + message_count / 4 + 4);
        } else {
            auto cut = std::find_if(slot.items.begin(), slot.items.end(),
                                    [restart](const TranscriptItem& item) {
                                        return static_cast<std::size_t>(item.lo) >=
                                               restart;
                                    });
            slot.items.erase(cut, slot.items.end());
        }

        build(restart, slot.items);
        slot.height = 0.0f;
        for (const TranscriptItem& item : slot.items) slot.height += item.height;
        slot.initialized = true;
        slot.data_identity = data_identity;
        slot.message_count = message_count;
        slot.revision = mutation.revision;
        slot.facts = facts;
        slot.dirty_from = no_restart();
        slot.force_full = false;
        return View{&slot.items, slot.height, message_count - restart, true,
                    restart == 0};
    }

    void invalidate(const std::string& key, std::size_t first) {
        auto found = slots_.find(key);
        if (found == slots_.end()) return;
        found->second.dirty_from = std::min(found->second.dirty_from, first);
    }

    void invalidate(const std::string& key) {
        auto found = slots_.find(key);
        if (found != slots_.end()) found->second.force_full = true;
    }

    void invalidate_all() {
        for (auto& entry : slots_) entry.second.force_full = true;
    }

    std::size_t slots() const { return slots_.size(); }

    std::size_t total_items() const {
        std::size_t total = 0;
        for (const auto& entry : slots_) total += entry.second.items.size();
        return total;
    }

    void clear() {
        slots_.clear();
        order_.clear();
    }

  private:
    struct Slot {
        std::vector<TranscriptItem> items;
        float height = 0.0f;
        bool initialized = false;
        const void* data_identity = nullptr;
        std::size_t message_count = 0;
        std::uint64_t revision = 0;
        TranscriptGeometryFacts facts;
        std::size_t dirty_from = no_restart();
        bool force_full = false;
        std::list<std::string>::iterator pos;
    };

    static constexpr std::size_t no_restart() {
        return std::numeric_limits<std::size_t>::max();
    }

    static std::size_t earlier_boundary(int a, int b) {
        if (a < 0) return b < 0 ? no_restart() : static_cast<std::size_t>(b);
        if (b < 0) return static_cast<std::size_t>(a);
        return static_cast<std::size_t>(std::min(a, b));
    }

    static bool global_facts_equal(const TranscriptGeometryFacts& a,
                                   const TranscriptGeometryFacts& b) {
        return a.pane_width == b.pane_width &&
               a.show_date_dividers == b.show_date_dividers &&
               a.show_reasoning == b.show_reasoning &&
               a.fold_long_messages == b.fold_long_messages &&
               a.tool_fold_mode == b.tool_fold_mode &&
               a.find_open == b.find_open && a.find_query == b.find_query &&
               a.font_epoch == b.font_epoch;
    }

    static bool facts_equal(const TranscriptGeometryFacts& a,
                            const TranscriptGeometryFacts& b) {
        return global_facts_equal(a, b) &&
               a.unread_first == b.unread_first &&
               a.unread_count == b.unread_count &&
               a.fold_revision == b.fold_revision &&
               a.streaming == b.streaming &&
               a.live_index == b.live_index &&
               a.stream_phase == b.stream_phase;
    }

    static std::size_t safe_restart(const std::vector<TranscriptItem>& items,
                                    std::size_t requested) {
        std::size_t restart = requested;
        for (const TranscriptItem& item : items) {
            const std::size_t lo = static_cast<std::size_t>(std::max(item.lo, 0));
            const std::size_t hi = item.hi > item.lo
                ? static_cast<std::size_t>(item.hi)
                : lo + 1;
            if (lo <= requested && requested < hi) restart = std::min(restart, lo);
            if (lo > requested) break;
        }
        return restart;
    }

    Slot& touch(const std::string& key) {
        auto found = slots_.find(key);
        if (found != slots_.end()) {
            order_.erase(found->second.pos);
            order_.push_front(key);
            found->second.pos = order_.begin();
            return found->second;
        }
        if (slots_.size() >= kMaxSlots) {
            slots_.erase(order_.back());
            order_.pop_back();
        }
        order_.push_front(key);
        Slot slot;
        slot.pos = order_.begin();
        auto [inserted, ok] = slots_.emplace(key, std::move(slot));
        (void)ok;
        return inserted->second;
    }

    std::unordered_map<std::string, Slot> slots_;
    std::list<std::string> order_;
};

inline TranscriptItemIndex& transcript_item_index() {
    static TranscriptItemIndex index;
    return index;
}

}  // namespace ecs::model
