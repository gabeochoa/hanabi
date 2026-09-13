#pragma once
// ---------------------------------------------------------------------------
// Saved views on disk: the JSON shape Settings keeps them in, and the read
// that refuses to destroy what it cannot parse.
//
// Two keys in the settings file: `saved_views` (an array of records, in shelf
// order) and `removed_builtin_views` (the ids of built-ins the reader deleted,
// sorted). A record that cannot be read is NOT dropped and NOT overwritten:
// `decode_views` answers nullopt, and the caller keeps the raw bytes under
// `saved_views_unreadable` so a future build can recover them (the
// reference's behaviour). A half-readable array -- one bad element -- is
// treated the same way, because silently keeping the other elements would
// save over the bad one on the next write.
//
// Pure over nlohmann::json; no file I/O. The Settings owner wires the two
// getters/setters; tests/unit/test_saved_views.cpp holds the round trip.
// ---------------------------------------------------------------------------
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"
#include "saved_views.h"

namespace hanabi::views {

inline constexpr std::string_view kSavedViewsKey = "saved_views";
inline constexpr std::string_view kRemovedBuiltInsKey = "removed_builtin_views";
inline constexpr std::string_view kUnreadableKey = "saved_views_unreadable";

[[nodiscard]] inline std::string_view scope_name(Scope s) {
    switch (s) {
        case Scope::Catalog: return "catalog";
        case Scope::Pinned: return "pinned";
        case Scope::Archived: return "archived";
        case Scope::Settings: return "settings";
    }
    return "catalog";
}

[[nodiscard]] inline std::optional<Scope> scope_from(std::string_view s) {
    if (s == "catalog") return Scope::Catalog;
    if (s == "pinned") return Scope::Pinned;
    if (s == "archived") return Scope::Archived;
    if (s == "settings") return Scope::Settings;
    return std::nullopt;
}

[[nodiscard]] inline std::string_view attention_name(Attention a) {
    switch (a) {
        case Attention::Any: return "any";
        case Attention::Blocked: return "blocked";
        case Attention::Review: return "review";
    }
    return "any";
}

[[nodiscard]] inline std::optional<Attention> attention_from(std::string_view s) {
    if (s == "any") return Attention::Any;
    if (s == "blocked") return Attention::Blocked;
    if (s == "review") return Attention::Review;
    return std::nullopt;
}

[[nodiscard]] inline nlohmann::json to_json(const SavedView& v) {
    nlohmann::json j = {
        {"id", v.id},
        {"name", v.name},
        {"glyph", v.glyph},
        {"scope", std::string(scope_name(v.scope))},
        {"attention", std::string(attention_name(v.attention))},
        {"query", v.query},
        {"built_in", v.builtIn},
    };
    if (!v.workspace.empty()) j["workspace"] = v.workspace;
    return j;
}

// nullopt when the record is not a saved view: missing or non-string id or
// name, an unknown scope or attention word, or a non-object.
[[nodiscard]] inline std::optional<SavedView> view_from_json(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    if (!j.contains("id") || !j["id"].is_string()) return std::nullopt;
    if (!j.contains("name") || !j["name"].is_string()) return std::nullopt;
    SavedView v;
    v.id = j["id"].get<std::string>();
    v.name = j["name"].get<std::string>();
    if (v.id.empty()) return std::nullopt;
    if (j.contains("glyph") && j["glyph"].is_string()) v.glyph = j["glyph"].get<std::string>();
    if (j.contains("scope")) {
        if (!j["scope"].is_string()) return std::nullopt;
        const auto s = scope_from(j["scope"].get<std::string>());
        if (!s) return std::nullopt;
        v.scope = *s;
    }
    if (j.contains("attention")) {
        if (!j["attention"].is_string()) return std::nullopt;
        const auto a = attention_from(j["attention"].get<std::string>());
        if (!a) return std::nullopt;
        v.attention = *a;
    }
    if (j.contains("workspace") && j["workspace"].is_string())
        v.workspace = j["workspace"].get<std::string>();
    if (j.contains("query") && j["query"].is_string()) v.query = j["query"].get<std::string>();
    if (j.contains("built_in") && j["built_in"].is_boolean()) v.builtIn = j["built_in"].get<bool>();
    return v;
}

[[nodiscard]] inline nlohmann::json encode_views(const std::vector<SavedView>& views) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& v : views) arr.push_back(to_json(v));
    return arr;
}

// The whole array or nothing: one unreadable element makes the record
// unreadable, so the caller preserves it rather than saving over it.
[[nodiscard]] inline std::optional<std::vector<SavedView>> decode_views(
    const nlohmann::json& j) {
    if (!j.is_array()) return std::nullopt;
    std::vector<SavedView> out;
    out.reserve(j.size());
    for (const auto& e : j) {
        auto v = view_from_json(e);
        if (!v) return std::nullopt;
        out.push_back(std::move(*v));
    }
    return out;
}

[[nodiscard]] inline nlohmann::json encode_removed(const std::vector<std::string>& ids) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& id : ids) arr.push_back(id);
    return arr;
}

// Lenient on purpose: an id that is not a string is skipped, not fatal --
// this list only ever withholds a default, so the worst a bad entry can do
// is bring a built-in back.
[[nodiscard]] inline std::vector<std::string> decode_removed(const nlohmann::json& j) {
    std::vector<std::string> out;
    if (!j.is_array()) return out;
    for (const auto& e : j)
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

// What a Settings loader does with the two keys, in one place: a Store, plus
// the raw bytes to preserve when the record could not be read.
struct Loaded {
    Store store;
    std::optional<std::string> unreadable;  // the raw `saved_views` text, if kept
};

[[nodiscard]] inline Loaded load(const nlohmann::json& settings) {
    Loaded out;
    std::vector<std::string> removed;
    if (settings.contains(std::string(kRemovedBuiltInsKey)))
        removed = decode_removed(settings[std::string(kRemovedBuiltInsKey)]);
    std::vector<SavedView> stored;
    if (settings.contains(std::string(kSavedViewsKey))) {
        const auto& raw = settings[std::string(kSavedViewsKey)];
        if (auto v = decode_views(raw)) stored = std::move(*v);
        else out.unreadable = raw.dump();
    }
    out.store = Store(std::move(stored), std::move(removed));
    return out;
}

// The two keys, written; when `unreadable` is set the array is NOT written
// (the caller keeps the old bytes) and the raw text goes under the
// preservation key instead.
inline void save(nlohmann::json& settings, const Store& store,
                 const std::optional<std::string>& unreadable) {
    if (unreadable) {
        settings[std::string(kUnreadableKey)] = *unreadable;
    } else {
        settings[std::string(kSavedViewsKey)] = encode_views(store.all());
    }
    settings[std::string(kRemovedBuiltInsKey)] = encode_removed(store.removed_built_ins());
}

}  // namespace hanabi::views
