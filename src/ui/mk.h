#pragma once

// ---------------------------------------------------------------------------
// A widget identity key that does not build a string to throw it away.
//
// afterhours' `imm::mk()` is called once per widget per frame -- it is what
// gives an immediate-mode widget a stable entity across frames. To do that it
// needs a key, and it builds one like this
// (vendor/afterhours/src/plugins/ui/entity_management.h:29):
//
//     std::stringstream pre_hash;
//     pre_hash << parent.id << otherID << "file: " << location.file_name()
//              << '(' << location.line() << ':' << location.column() << ") `"
//              << location.function_name() << "`: " << '\n';
//     UI_UUID hash = std::hash<std::string>{}(pre_hash.str());
//
// A `std::stringstream`, an absolute source path, and the full expanded
// function signature -- for this app that is routinely 200+ characters, built
// character by character through `stringbuf::overflow`, hashed, and destroyed,
// for every widget, 60 times a second. Measured with HANABI_PROF_SITES=1 at
// 2000 sessions it is 2,164 allocations a frame on Home and 3,063 with a
// 480-message thread open: 64% and 53% of every allocation the app makes,
// spent on a number. (scripts/alloc_gate.sh's one-line rehearsal points this
// wrapper back at the library's and reads the difference.)
//
// vendor/afterhours is read-only (~20 projects vendor it), so this is the
// app-side workaround and afterhours_gaps.md #180 is the upstream ask.
//
// WHAT THIS KEEPS. The key is a hash of exactly the same five facts -- parent
// id, caller-supplied index, file, function, line:column -- so two call sites
// collide exactly when they collided before. It is stored in afterhours' own
// `existing_ui_elements` map and looked up through afterhours' own collection
// holder, so a widget made here is indistinguishable from one made there, and
// `clear_existing_ui_elements()` still clears it.
//
// WHAT IT CHANGES. The file and function are hashed BY POINTER rather than by
// content. `source_location`'s strings have static storage duration, so the
// pointer is stable for the life of the process, which is the only property
// the key needs: it is never persisted, never compared across processes, and
// never shown to anyone. Numerically the hashes differ from upstream's, which
// is what keeps a hanabi widget from ever aliasing an afterhours-internal one
// in the shared map.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <source_location>

#include "../../vendor/afterhours/src/plugins/ui/entity_management.h"
#include "../../vendor/afterhours/src/plugins/ui/ui_collection.h"

namespace hanabi::ui {

inline afterhours::ui::imm::UI_UUID widget_key(
    afterhours::EntityID parentID, afterhours::EntityID otherID,
    const std::source_location& loc) {
    const auto mix = [](std::uint64_t h, std::uint64_t v) {
        return (h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))) *
               0x100000001B3ull;
    };
    std::uint64_t h = 0xCBF29CE484222325ull;
    h = mix(h, static_cast<std::uint32_t>(parentID));
    h = mix(h, static_cast<std::uint32_t>(otherID));
    h = mix(h, reinterpret_cast<std::uintptr_t>(loc.file_name()));
    h = mix(h, reinterpret_cast<std::uintptr_t>(loc.function_name()));
    h = mix(h, (static_cast<std::uint64_t>(loc.line()) << 20) ^ loc.column());
    return static_cast<afterhours::ui::imm::UI_UUID>(h);
}

inline afterhours::ui::imm::EntityParent mk(
    afterhours::Entity& parent, afterhours::EntityID otherID = -1,
    const std::source_location loc = std::source_location::current()) {
    const afterhours::ui::imm::UI_UUID hash =
        widget_key(parent.id, otherID, loc);
    auto& live = afterhours::ui::imm::existing_ui_elements;
    if (const auto it = live.find(hash); it != live.end()) {
        try {
            it->second.last_built_frame =
                afterhours::ui::imm::ui_build_frame;
            return {afterhours::ui::UICollectionHolder::getEntityForIDEnforce(
                        it->second.id),
                    parent};
        } catch (const std::bad_optional_access&) {
            log_error("Entity ID conflict detected! mk() was called more than "
                      "once from the same source location without an index. "
                      "Location: {}:{}:{}, Function: {}. Use mk(parent, index) "
                      "with unique indices.",
                      loc.file_name(), loc.line(), loc.column(),
                      loc.function_name());
            throw;
        }
    }
    afterhours::Entity& entity =
        afterhours::ui::UICollectionHolder::get().collection.createEntity();
    live[hash] = afterhours::ui::imm::UIElementRecord{
        entity.id, afterhours::ui::imm::ui_build_frame, 0};
    return {entity, parent};
}

}  // namespace hanabi::ui
