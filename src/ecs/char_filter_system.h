#pragma once
// ComposerCharFilterSystem — strips control-code CHAR events from the input
// char queue BEFORE any text_input widget drains them.
//
// WHY: on macOS the sokol backend emits a SAPP_EVENTTYPE_CHAR for keys whose
// NSEvent.characters is non-empty — including BACKSPACE (U+007F DEL). The
// afterhours char queue pushes any char_code > 0, and afterhours' insert_char
// only rejects codepoints < 32 — so 0x7F slips through and is TYPED as a DEL
// glyph ("backspace adds a space/box"; the stray control byte also corrupts the
// field so real space/edits misbehave). We can't edit vendored afterhours, so
// we drain the queue each frame, drop non-typable codepoints (see
// is_typable_char), and re-push the good ones in order — the widget then only
// ever pops real characters. Logged as afterhours gap #31.
//
// Runs BEFORE the UI-creating systems (registered right after the pre-layout
// systems) so the filtered queue is what MainPaneSystem's text_input sees.

#include "ui_imports.h"
#include "../api/textinput_filter.h"

#ifdef AFTER_HOURS_USE_METAL
#include <afterhours/src/backends/sokol/backend.h>
#endif

namespace ecs {

struct ComposerCharFilterSystem : afterhours::System<> {
    void once(const float) override {
#ifdef AFTER_HOURS_USE_METAL
        using namespace afterhours::graphics::metal_detail;
        // Drain the whole queue, keeping only typable codepoints, then re-push
        // them in their original order. Bounded by CHAR_QUEUE_SIZE so this is
        // O(queue) and never spins.
        uint32_t kept[afterhours::graphics::metal_detail::InputState::
                          CHAR_QUEUE_SIZE];
        int n = 0;
        for (uint32_t c = pop_char(); c != 0; c = pop_char()) {
            if (hanabi::is_typable_char(static_cast<int>(c)) &&
                n < static_cast<int>(
                        afterhours::graphics::metal_detail::InputState::
                            CHAR_QUEUE_SIZE))
                kept[n++] = c;
        }
        for (int i = 0; i < n; ++i) push_char(kept[i]);
#endif
    }
};

}  // namespace ecs
