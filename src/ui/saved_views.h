#pragma once
// ---------------------------------------------------------------------------
// Saved views: the shelves the sidebar's Views header lists, and the store
// that keeps them.
//
// The reference's sidebar has a `+` on its Views header and a "Save Current
// Filter As…" item in the shelf menu; both keep the state the reader is
// looking at -- the view, the workspace, the attention matcher and the search
// query -- as a shelf of its own, select it, and clear the search field (the
// query is IN the shelf now). "Restore Default Views" is offered only while a
// built-in is missing, and brings the missing ones back at their definition
// rank. Rename is withheld on a built-in (it is re-adopted from its definition
// at every launch, so a new name would revert overnight); Delete is offered on
// a built-in, and Restore is the way back.
//
// This is the MODEL half: pure, no UI, no file I/O. The sidebar renders
// `Store::shelves()`; Settings persists `Store::all()` and
// `Store::removed_built_ins()` through saved_views_codec.h. Both halves are
// held by tests/unit/test_saved_views.cpp without a window.
//
// Names are hanabi's. Behaviour follows the pinned reference; no code was
// copied from it.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <cstdio>
#include <utility>
#include <vector>

#include "../api/types.h"
#include "../ecs/thread_model.h"
#include "../util/format.h"

namespace hanabi::views {

// Which list a shelf reads from. The reference's fifth scope (a chat surface
// hanabi does not have) is omitted rather than carried as dead vocabulary.
enum class Scope { Catalog, Pinned, Archived, Settings };

// Which threads' attention a shelf keeps. The reference matches on a set of
// attention kinds; the three sets it ever builds are these.
enum class Attention { Any, Blocked, Review };

struct SavedView {
    std::string id;
    std::string name;
    std::string glyph;  // an icon name in hanabi's atlas vocabulary
    Scope scope = Scope::Catalog;
    Attention attention = Attention::Any;
    std::string workspace;  // "" = any; matched against SessionSummary::folder
    std::string query;      // "" = no text filter
    bool builtIn = false;

    bool operator==(const SavedView&) const = default;

    // A shelf that shows conversations, as opposed to the Settings surface.
    [[nodiscard]] bool shows_threads() const { return scope != Scope::Settings; }
    [[nodiscard]] bool shows_attention_badge() const { return scope == Scope::Catalog; }
};

// ── Built-ins ─────────────────────────────────────────────────────────────
// Ids are the literal words; a stored record naming one always comes back as
// the app's definition, so a built-in's name and glyph cannot drift on disk.

inline constexpr std::string_view kHomeId = "home";
inline constexpr std::string_view kSettingsId = "settings";
inline constexpr std::string_view kBlockedId = "blocked";
inline constexpr std::string_view kReviewId = "review";
inline constexpr std::string_view kPinnedId = "pinned";
inline constexpr std::string_view kArchivedId = "archived";

inline const std::vector<SavedView>& built_ins() {
    static const std::vector<SavedView> defs = {
        {std::string(kHomeId), "Home", "house", Scope::Catalog, Attention::Any, "", "", true},
        {std::string(kSettingsId), "Settings", "settings", Scope::Settings, Attention::Any, "", "", true},
        {std::string(kBlockedId), "Blocked", "hand", Scope::Catalog, Attention::Blocked, "", "", true},
        {std::string(kReviewId), "Review", "check_circle", Scope::Catalog, Attention::Review, "", "", true},
        {std::string(kPinnedId), "Pinned", "pin", Scope::Pinned, Attention::Any, "", "", true},
        {std::string(kArchivedId), "Archived", "archive", Scope::Archived, Attention::Any, "", "", true},
    };
    return defs;
}

[[nodiscard]] inline const SavedView* built_in(std::string_view id) {
    for (const auto& v : built_ins())
        if (v.id == id) return &v;
    return nullptr;
}

[[nodiscard]] inline int built_in_rank(std::string_view id) {
    const auto& defs = built_ins();
    for (std::size_t i = 0; i < defs.size(); ++i)
        if (defs[i].id == id) return static_cast<int>(i);
    return -1;
}

// ── Matching ──────────────────────────────────────────────────────────────

[[nodiscard]] inline bool attention_keeps(Attention a, const api::SessionSummary& s) {
    switch (a) {
        case Attention::Any: return true;
        case Attention::Blocked: return ecs::model::in_blocked_view(s);
        case Attention::Review: return ecs::model::in_review_view(s);
    }
    return true;
}

[[nodiscard]] inline std::string trimmed(std::string_view s) {
    const auto ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return std::string(s.substr(b, e - b + 1));
}

[[nodiscard]] inline bool contains_ci(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size() && ok; ++j)
            ok = fmtutil::lower_ch(hay[i + j]) == fmtutil::lower_ch(needle[j]);
        if (ok) return true;
    }
    return false;
}

// Whether a shelf shows this thread. Scope decides the list (pinned and
// archived read the catalog through their own predicate; the catalog shelves
// exclude archived threads, as the sidebar already does); then attention,
// then workspace, then the query against the displayed title.
[[nodiscard]] inline bool keeps(const SavedView& v, const api::SessionSummary& s) {
    switch (v.scope) {
        case Scope::Settings: return false;
        case Scope::Pinned:
            if (!ecs::model::in_starred_view(s)) return false;
            break;
        case Scope::Archived:
            if (!ecs::model::in_archived_view(s)) return false;
            break;
        case Scope::Catalog:
            if (ecs::model::is_archived(s)) return false;
            break;
    }
    if (!attention_keeps(v.attention, s)) return false;
    if (!v.workspace.empty() && s.folder != v.workspace) return false;
    const std::string wanted = trimmed(v.query);
    if (wanted.empty()) return true;
    return contains_ci(fmtutil::display_title_view(s.title), wanted);
}

struct AttentionCounts {
    int home = 0;     // blocked + review
    int blocked = 0;
    int review = 0;
};

[[nodiscard]] inline AttentionCounts attention_counts(
    const std::vector<api::SessionSummary>& sessions) {
    AttentionCounts c;
    for (const auto& s : sessions) {
        if (ecs::model::is_archived(s)) continue;
        if (ecs::model::in_blocked_view(s)) ++c.blocked;
        else if (ecs::model::in_review_view(s)) ++c.review;
    }
    c.home = c.blocked + c.review;
    return c;
}

// ── Drafting a new shelf from what is on screen ───────────────────────────

inline constexpr std::string_view kDraftGlyph = "filter";
inline constexpr std::string_view kNameSeparator = " \xc2\xb7 ";  // " · "

[[nodiscard]] inline bool usable_name(std::string_view name) {
    return !trimmed(name).empty();
}

// "<workspace leaf> · <view name> · <query>", each part only when present;
// "Filter" when none is.
[[nodiscard]] inline std::string derived_name(const SavedView& current,
                                              std::string_view workspace,
                                              std::string_view query) {
    std::vector<std::string> parts;
    if (!workspace.empty()) {
        const auto slash = workspace.find_last_of('/');
        std::string_view leaf =
            slash == std::string_view::npos ? workspace : workspace.substr(slash + 1);
        parts.emplace_back(leaf.empty() ? workspace : leaf);
    }
    if (!current.name.empty()) parts.push_back(current.name);
    if (const std::string q = trimmed(query); !q.empty()) parts.push_back(q);
    if (parts.empty()) return "Filter";
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += kNameSeparator;
        out += parts[i];
    }
    return out;
}

// A fresh id: not a built-in word, unique per process. Callers may replace
// `salt` with a real random source; the store refuses a duplicate id anyway.
[[nodiscard]] inline std::string fresh_id(std::uint64_t salt = 0) {
    static std::uint64_t counter = 0;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    char buf[40];
    std::snprintf(buf, sizeof buf, "v-%016llx-%04llx",
                  static_cast<unsigned long long>(now ^ salt),
                  static_cast<unsigned long long>(++counter & 0xffff));
    return buf;
}

[[nodiscard]] inline SavedView make_from(const SavedView& current,
                                         std::string_view workspace,
                                         std::string_view query,
                                         std::string_view name,
                                         std::string id = fresh_id()) {
    SavedView v;
    v.id = std::move(id);
    v.name = trimmed(name);
    v.glyph = std::string(kDraftGlyph);
    v.scope = current.scope;
    v.attention = current.attention;
    v.workspace = std::string(workspace);
    v.query = trimmed(query);
    v.builtIn = false;
    return v;
}

// ── The store ─────────────────────────────────────────────────────────────

class Store {
   public:
    Store() : Store({}, {}) {}

    // The reference's seeding: stored records in their stored order, a
    // built-in id always replaced by the app's definition, a stored record
    // CLAIMING to be built-in under an unknown id dropped, then every
    // built-in not present and not remembered as removed appended in
    // definition order.
    Store(std::vector<SavedView> stored, std::vector<std::string> removedBuiltIns)
        : removed_(std::move(removedBuiltIns)) {
        std::sort(removed_.begin(), removed_.end());
        removed_.erase(std::unique(removed_.begin(), removed_.end()), removed_.end());
        for (auto& entry : stored) {
            if (const SavedView* def = built_in(entry.id)) {
                if (find(def->id) == nullptr) views_.push_back(*def);
            } else if (!entry.builtIn && !entry.id.empty() && find(entry.id) == nullptr) {
                views_.push_back(std::move(entry));
            }
        }
        for (const auto& def : built_ins())
            if (find(def.id) == nullptr && !is_removed(def.id)) views_.push_back(def);
    }

    [[nodiscard]] const std::vector<SavedView>& shelves() const { return views_; }
    [[nodiscard]] const std::vector<SavedView>& all() const { return views_; }
    [[nodiscard]] const std::vector<std::string>& removed_built_ins() const { return removed_; }

    [[nodiscard]] const SavedView* find(std::string_view id) const {
        for (const auto& v : views_)
            if (v.id == id) return &v;
        return nullptr;
    }

    // The shelf to show for a selection that may name nothing (or nothing
    // that still exists): Home when present, else the first shelf, else the
    // Home definition itself.
    [[nodiscard]] const SavedView& resolve(std::optional<std::string_view> id) const {
        if (id)
            if (const SavedView* v = find(*id)) return *v;
        if (const SavedView* home = find(kHomeId)) return *home;
        if (!views_.empty()) return views_.front();
        return built_ins().front();
    }

    // False for a duplicate id, an empty id, or a name usable_name() refuses:
    // the UI shows its prompt again rather than dropping the save silently.
    bool add(SavedView v) {
        if (v.id.empty() || find(v.id) != nullptr) return false;
        if (!usable_name(v.name)) return false;
        v.name = trimmed(v.name);
        views_.push_back(std::move(v));
        return true;
    }

    // User views only: a built-in is its definition and cannot be edited.
    bool update(const SavedView& v) {
        if (v.builtIn || built_in(v.id) != nullptr) return false;
        for (auto& existing : views_)
            if (existing.id == v.id) {
                existing = v;
                return true;
            }
        return false;
    }

    // Allowed on a built-in; the removal is remembered so seeding does not
    // bring it straight back. restore_built_in / restore_defaults undo it.
    bool remove(std::string_view id) {
        const auto it = std::find_if(views_.begin(), views_.end(),
                                     [&](const SavedView& v) { return v.id == id; });
        if (it == views_.end()) return false;
        if (it->builtIn && !is_removed(it->id)) {
            removed_.push_back(it->id);
            std::sort(removed_.begin(), removed_.end());
        }
        views_.erase(it);
        return true;
    }

    // Re-inserted before the first shelf whose built-in rank is higher, so
    // the defaults keep their order among themselves wherever the reader's
    // own shelves sit.
    bool restore_built_in(std::string_view id) {
        const SavedView* def = built_in(id);
        if (def == nullptr || find(id) != nullptr) return false;
        const int rank = built_in_rank(id);
        auto at = views_.end();
        for (auto it = views_.begin(); it != views_.end(); ++it) {
            const int r = built_in_rank(it->id);
            if (r > rank) {
                at = it;
                break;
            }
        }
        views_.insert(at, *def);
        removed_.erase(std::remove(removed_.begin(), removed_.end(), std::string(id)),
                       removed_.end());
        return true;
    }

    // User views only; trimmed; a no-op rename says false.
    bool rename(std::string_view id, std::string_view name) {
        const std::string wanted = trimmed(name);
        if (wanted.empty()) return false;
        for (auto& v : views_)
            if (v.id == id) {
                if (v.builtIn || built_in(v.id) != nullptr || v.name == wanted) return false;
                v.name = wanted;
                return true;
            }
        return false;
    }

    bool move(std::size_t from, std::size_t to) {
        if (from >= views_.size() || to >= views_.size() || from == to) return false;
        SavedView moved = std::move(views_[from]);
        views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(from));
        views_.insert(views_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
        return true;
    }

    // How many came back. Offered by the UI only while has_deleted_built_ins().
    int restore_defaults() {
        int restored = 0;
        for (const auto& def : built_ins())
            if (find(def.id) == nullptr && restore_built_in(def.id)) ++restored;
        return restored;
    }

    [[nodiscard]] bool has_deleted_built_ins() const {
        for (const auto& def : built_ins())
            if (find(def.id) == nullptr) return true;
        return false;
    }

   private:
    [[nodiscard]] bool is_removed(std::string_view id) const {
        return std::find(removed_.begin(), removed_.end(), std::string(id)) != removed_.end();
    }

    std::vector<SavedView> views_;
    std::vector<std::string> removed_;
};

}  // namespace hanabi::views
