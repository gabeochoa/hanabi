#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "../../vendor/afterhours/src/plugins/ui/entity_management.h"
#include "../../vendor/afterhours/src/plugins/ui/ui_collection.h"
#include "mk.h"

namespace hanabi::widget_epoch {

using afterhours::EntityID;

inline bool env_flag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return fallback;
    return *value != '0';
}

inline unsigned env_uint(const char* name, unsigned fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return fallback;
    const int parsed = std::atoi(value);
    return parsed > 0 ? static_cast<unsigned>(parsed) : fallback;
}

inline bool retire_enabled() {
    static const bool enabled = env_flag("HANABI_RETIRE", true);
    return enabled;
}

inline unsigned grace_frames() {
    static const unsigned grace = env_uint("HANABI_RETIRE_GRACE", 90);
    return grace;
}

inline void configure_retirement() {
    afterhours::ui::imm::ui_retire_grace_frames =
        retire_enabled() ? grace_frames() : 0;
}

inline unsigned epoch() {
    return static_cast<unsigned>(afterhours::ui::imm::ui_build_frame);
}

inline size_t built_this_epoch() {
    size_t newest = 0;
    for (const auto& [_, record] : afterhours::ui::imm::existing_ui_elements)
        newest = std::max(newest, record.last_built_frame);
    size_t count = 0;
    for (const auto& [_, record] : afterhours::ui::imm::existing_ui_elements)
        if (record.last_built_frame == newest) ++count;
    return count;
}

inline unsigned stamp_read(EntityID id) {
    for (const auto& [_, record] : afterhours::ui::imm::existing_ui_elements)
        if (record.id == id)
            return static_cast<unsigned>(record.last_built_frame + 1);
    return 0;
}

struct Tally {
    size_t live = 0;
    size_t stamped = 0;
    size_t built_this_frame = 0;
    size_t stale = 0;
    size_t unstamped = 0;
};

inline Tally tally() {
    Tally out;
    for (const auto& entity :
         afterhours::ui::UICollectionHolder::get().collection.get_entities())
        if (entity) ++out.live;
    out.stamped = afterhours::ui::imm::existing_ui_elements.size();
    out.built_this_frame = built_this_epoch();
    out.stale = out.stamped - out.built_this_frame;
    out.unstamped = out.live > out.stamped ? out.live - out.stamped : 0;
    return out;
}

inline size_t unretired_stale_count(unsigned grace) {
    const size_t frame = afterhours::ui::imm::ui_build_frame;
    size_t count = 0;
    for (const auto& [_, record] : afterhours::ui::imm::existing_ui_elements)
        if (frame > record.last_built_frame + grace) ++count;
    return count;
}

}  // namespace hanabi::widget_epoch
