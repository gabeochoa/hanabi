#pragma once

// ---------------------------------------------------------------------------
// Two-step Escape for the composer.
//
// WHAT WAS THERE. One Escape emptied the box. escape_system.h already ranks
// Escape so an overlay eats the press before the composer sees it, but once
// the press did reach the composer it destroyed the draft outright, with no
// warning and no undo -- so a reader dismissing something with a stale caret in
// the field lost what they had typed. That single keystroke was the most
// destructive divergence in the new-thread surface.
//
// A DRAFT IS NOT ONLY ITS TEXT. The first version of this rule took a single
// `draft` string, so a composer holding three staged files and no typed words
// answered "empty" -- and the empty arm CLOSES the surface. One unwarned press
// took the reader off a surface with their files on it. Every branch here now
// asks `has_content()`, which is text OR staged files, and the arm is bound to
// an IDENTITY that spans both, so removing a file under a live arm retires it
// exactly as typing does.
//
// WHAT THIS IS. The whole rule, as a total function over one press, kept apart
// from the frame so a unit test can drive every arm without a window:
//
//   1. Something transient is up (a menu, a popover, a notice) -> dismiss THAT
//      and disarm. The draft is never the first thing a press reaches.
//   2. Otherwise the field has TEXT -> the first press ARMS and says so; a
//      second press inside kArmWindowMs clears the text. Staged files are not
//      the box's contents and are never cleared by a keystroke: they have
//      their own per-file remove, which is explicit and visible.
//   3. Otherwise there are STAGED FILES and no text -> the press protects
//      them and says so, and says so again on the next press. There is nothing
//      to clear, and closing the surface over someone's staged files is the
//      defect this arm exists to refuse.
//   4. Otherwise the composer is genuinely empty -> the press closes the
//      surface if there is somewhere to go back to, else it hands the keyboard
//      back, else it does nothing. An empty composer is never "armed": there
//      is nothing to protect and a hint about clearing nothing is a lie.
//
// The window is 1.5s rather than the 0.5s a bare double-press would want,
// because step 2 puts a sentence on screen and the reader has to read it.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

namespace ecs::model {

// The clock the arm is measured against. Steady, so a wall-clock adjustment
// cannot make a live arm look expired (or a dead one look live).
inline std::uint64_t escape_now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

inline constexpr std::uint64_t kComposerEscapeArmWindowMs = 1500;

// What one press did. Every arm is reachable and every arm is asserted.
enum class ComposerEscapeStep {
    Ignored,
    DismissedTransient,
    ReleasedFocus,
    Armed,
    Cleared,
    KeptStaged,
    ClosedSurface,
};

inline const char* name_of(ComposerEscapeStep step) {
    switch (step) {
        case ComposerEscapeStep::Ignored: return "ignored";
        case ComposerEscapeStep::DismissedTransient: return "dismissed";
        case ComposerEscapeStep::ReleasedFocus: return "released-focus";
        case ComposerEscapeStep::Armed: return "armed";
        case ComposerEscapeStep::Cleared: return "cleared";
        case ComposerEscapeStep::KeptStaged: return "kept-staged";
        case ComposerEscapeStep::ClosedSurface: return "closed-surface";
    }
    return "ignored";
}

// The two sentences the protected states put on screen. Held here beside the
// rule that raises them so the copy cannot drift away from the behaviour it
// describes -- and they say DIFFERENT things because the two states do: one
// warns what the next press will destroy, the other says what this press
// refused to do.
inline constexpr const char* kComposerEscapeArmedHint =
    "Press Escape again to clear the draft";
inline constexpr const char* kComposerEscapeStagedHint =
    "Files are still attached \xc2\xb7 remove them to close";

struct ComposerEscapeState {
    bool armed = false;
    std::uint64_t armed_at_ms = 0;
    // The draft IDENTITY the arm was taken for -- text and staged files
    // together. An arm is only good for what the reader was looking at when
    // they pressed.
    std::string armed_for;
    // The pane+draft slot the arm belongs to, so a tab switch cannot carry a
    // live arm into another thread's composer.
    std::string armed_slot;

    void disarm() {
        armed = false;
        armed_at_ms = 0;
        armed_for.clear();
        armed_slot.clear();
    }

    [[nodiscard]] bool armed_for_now(const std::string& slot,
                                     const std::string& identity,
                                     std::uint64_t now_ms) const {
        return armed && armed_slot == slot && armed_for == identity &&
               now_ms - armed_at_ms <= kComposerEscapeArmWindowMs;
    }
};

struct ComposerEscapeInput {
    // True when this frame's Escape belongs to something else that is up --
    // resolved by escape_system.h, which is the one reader of the key.
    bool transient_up = false;
    bool field_focused = false;
    // Somewhere to go back to when the composer is empty (the thread the
    // new-thread surface was opened over).
    bool surface_can_close = false;
    std::string slot;
    // The two halves of a draft. Attachments are their ORDERED names: order is
    // part of what the reader staged, so a reorder must retire an arm too.
    std::string text;
    std::vector<std::string> attachments;
    std::uint64_t now_ms = 0;

    [[nodiscard]] bool has_content() const {
        return !text.empty() || !attachments.empty();
    }

    // One string standing for everything the composer is holding. Unit
    // separator between the halves so a file named like the draft's text
    // cannot make two different states compare equal.
    [[nodiscard]] std::string identity() const {
        std::string out = text;
        for (const std::string& name : attachments) {
            out += '\x1f';
            out += name;
        }
        return out;
    }
};

inline ComposerEscapeStep resolve_composer_escape(
    ComposerEscapeState& state, const ComposerEscapeInput& in) {
    if (in.transient_up) {
        state.disarm();
        return ComposerEscapeStep::DismissedTransient;
    }
    const std::string identity = in.identity();
    if (!in.text.empty()) {
        if (state.armed_for_now(in.slot, identity, in.now_ms)) {
            state.disarm();
            return ComposerEscapeStep::Cleared;
        }
        state.armed = true;
        state.armed_at_ms = in.now_ms;
        state.armed_for = identity;
        state.armed_slot = in.slot;
        return ComposerEscapeStep::Armed;
    }
    if (!in.attachments.empty()) {
        // Armed so the sentence is on screen and expires like any other, but
        // a SECOND press lands here again rather than clearing: there is no
        // text to clear, and the files are not Escape's to take.
        state.armed = true;
        state.armed_at_ms = in.now_ms;
        state.armed_for = identity;
        state.armed_slot = in.slot;
        return ComposerEscapeStep::KeptStaged;
    }
    state.disarm();
    if (in.surface_can_close) return ComposerEscapeStep::ClosedSurface;
    if (in.field_focused) return ComposerEscapeStep::ReleasedFocus;
    return ComposerEscapeStep::Ignored;
}

// Which sentence a live arm is showing. The two protected states share one
// timer and one field, so the state alone cannot say which -- what the
// composer is holding decides, the same way it decided the step.
inline const char* armed_hint_for(const ComposerEscapeInput& in) {
    return in.text.empty() ? kComposerEscapeStagedHint
                           : kComposerEscapeArmedHint;
}

// A draft that changed under a live arm retires it: the reader typed, staged
// or removed something, so the next Escape is a first press again. Called
// every frame, not only on a press.
inline void retire_stale_arm(ComposerEscapeState& state,
                             const std::string& slot,
                             const std::string& identity,
                             std::uint64_t now_ms) {
    if (!state.armed) return;
    if (state.armed_slot != slot || state.armed_for != identity ||
        now_ms - state.armed_at_ms > kComposerEscapeArmWindowMs)
        state.disarm();
}

}  // namespace ecs::model
