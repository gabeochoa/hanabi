#pragma once

// ---------------------------------------------------------------------------
// A bounded LRU keyed by (text, two floats), for memoizing a pure function of
// a string and a width.
//
// WHY THIS EXISTS AS A TYPE. hanabi now has four memos in front of text
// measurement -- the transcript's per-message render cache, its hug memo, the
// sidebar's ellipsis memo and the line-count memo this file was written for --
// and they had four different bounds, of three different kinds: "clear the
// whole thing when it hits 4096", "hold two width slots and evict the older",
// and "keep three threads". Two of those are correct for their shape and one
// (the wholesale clear) throws away the working set to punish the tail.
//
// The three properties that are not obvious and are the reason this is
// shared code rather than a fourth hand-rolled map:
//
//   1. THE BOUND IS AN EVICTION, NOT A CLEAR. A cache that empties itself at
//      the cap pays one cold frame for every entry it was holding, and does
//      it precisely when the working set is largest -- a resize drag, or a
//      fling through a long list. Evicting the least recently used costs one
//      recompute.
//   2. A LOOKUP ALLOCATES NOTHING. The map is heterogeneous: a string_view
//      searches it, so a hit is a hash and a compare. The sidebar's memo
//      carries the note explaining why this matters -- an owning key built
//      per lookup put the function back at 15% of the main thread after the
//      quadratic measurement had already been removed. Replacing measurement
//      with a malloc is a smaller win than it sounds.
//   3. THE KEY VIEWS THE ENTRY'S OWN STRING. Entries live in a std::list, so
//      their addresses are stable across insert and erase, and the map holds
//      a view into the entry it points at. One copy of the text, not two.
//
// `size()` never exceeds `capacity()`, and tests/unit/test_text_cache.cpp
// pins that against 200x the capacity in distinct keys, along with what LRU
// order actually means at the boundary.
// ---------------------------------------------------------------------------

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace hanabi::text {

template <class V>
class TextKeyCache {
  public:
    explicit TextKeyCache(std::size_t capacity) : cap_(capacity ? capacity : 1) {}

    // The value for (text, a, b), or nullptr. A hit is promoted to most
    // recently used, which is what makes the bound cost one recompute rather
    // than evicting the thing about to be asked for again.
    const V* find(std::string_view text, float a, float b) {
        auto it = index_.find(KeyRef{text, a, b});
        if (it == index_.end()) return nullptr;
        entries_.splice(entries_.begin(), entries_, it->second);
        return &it->second->value;
    }

    const V& put(std::string_view text, float a, float b, V value) {
        if (auto it = index_.find(KeyRef{text, a, b}); it != index_.end()) {
            it->second->value = std::move(value);
            entries_.splice(entries_.begin(), entries_, it->second);
            return it->second->value;
        }
        while (entries_.size() >= cap_) evict_lru();
        entries_.push_front(Entry{std::string(text), a, b, std::move(value)});
        const auto pos = entries_.begin();
        index_.emplace(KeyRef{pos->text, a, b}, pos);
        return pos->value;
    }

    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return cap_; }

    void clear() {
        index_.clear();
        entries_.clear();
    }

  private:
    struct Entry {
        std::string text;
        float a = 0.0f;
        float b = 0.0f;
        V value;
    };

    // Hash the float BITS, not the value: two widths that differ by a hair
    // are different keys and must hash apart.
    struct KeyRef {
        std::string_view text;
        float a;
        float b;
    };
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(const KeyRef& k) const {
            std::size_t h = std::hash<std::string_view>{}(k.text);
            const auto mix = [&h](float f) {
                h ^= std::hash<std::uint32_t>{}(std::bit_cast<std::uint32_t>(f)) +
                     0x9e3779b9 + (h << 6) + (h >> 2);
            };
            mix(k.a);
            mix(k.b);
            return h;
        }
    };
    struct Eq {
        using is_transparent = void;
        bool operator()(const KeyRef& x, const KeyRef& y) const {
            return x.a == y.a && x.b == y.b && x.text == y.text;
        }
    };

    void evict_lru() {
        if (entries_.empty()) return;
        const Entry& back = entries_.back();
        index_.erase(KeyRef{back.text, back.a, back.b});
        entries_.pop_back();
    }

    std::size_t cap_;
    std::list<Entry> entries_;  // MRU front ... LRU back
    std::unordered_map<KeyRef, typename std::list<Entry>::iterator, Hash, Eq>
        index_;
};

}  // namespace hanabi::text
