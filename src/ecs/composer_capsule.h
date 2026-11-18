#pragma once
// Floating composer capsule (kt-xuit) — parity with the reference.
//
// Reference behaviour (9948bc337d64, Sources/Views/{AgentcloudChatView,
// ComposerPane}.swift): the conversation composer floats as a styled
// capsule detached from the pane edges; the queue line, the question
// dock and the notice row all live INSIDE the capsule; the capsule
// shape follows a composer-style setting (Glass / Card / Puffling).
//
// Hanabi today (verified on 261cde63): layout.composer is a full-width
// bar flush with the window floor; the ask card's height is added on
// top of the composer chrome (composerHeight = chrome + ask_card_h),
// so questions sit ABOVE the composer, while the notice row and the
// "N queued" status already live in the composer strip.
//
// Parity split:
//  (a) placement (behaviour): Hanabi's composerHeight already sums
//      chrome + ask card, so insetting the whole composer rect puts
//      queue/questions/notices inside the capsule in one move.
//  (b) styling (appearance): the capsule shape (detachment, corner
//      radius per style) is implemented natively here; the Glass
//      translucency itself diverges — Afterhours has no backdrop
//      blur, so Glass draws as Card with the Glass radius (documented
//      divergence, not a silent one).
#include <string_view>

namespace ecs::model {

enum class ComposerStyle { Glass, Card, Puffling };

inline ComposerStyle composer_style_from(std::string_view s) {
    if (s == "glass") return ComposerStyle::Glass;
    if (s == "puffling") return ComposerStyle::Puffling;
    return ComposerStyle::Card;
}
inline const char* name_of(ComposerStyle s) {
    switch (s) {
        case ComposerStyle::Glass: return "glass";
        case ComposerStyle::Card: return "card";
        case ComposerStyle::Puffling: return "puffling";
    }
    return "card";
}

// Detachment of the capsule from the pane edges (pt). The reference's
// launcher geometry pads its capsule rows 12pt leading / 8pt outer;
// the conversation capsule keeps the same order of margin.
inline constexpr float kCapsuleSideInset = 12.0f;
inline constexpr float kCapsuleBottomInset = 10.0f;
inline constexpr float kCapsuleTopGap = 6.0f;

inline float capsule_radius(ComposerStyle s) {
    switch (s) {
        case ComposerStyle::Glass: return 16.0f;
        case ComposerStyle::Card: return 10.0f;
        case ComposerStyle::Puffling: return 22.0f;
    }
    return 10.0f;
}

struct CapsuleRect { float x, y, w, h; };

// The detached capsule inside the flush composer bar `bar`
// (x,y,w,h): inset on both sides and the bottom, gap on top. A bar
// too small to inset keeps its size (never negative).
inline CapsuleRect capsule_rect(float x, float y, float w, float h) {
    const float iw = w - 2.0f * kCapsuleSideInset;
    const float ih = h - kCapsuleTopGap - kCapsuleBottomInset;
    return {x + kCapsuleSideInset, y + kCapsuleTopGap,
            iw > 0.0f ? iw : w, ih > 0.0f ? ih : h};
}

}  // namespace ecs::model
