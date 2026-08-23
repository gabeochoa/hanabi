#pragma once

// Pure, graphics-free thread-model decision logic.
//
// This centralizes the HIGH-SIGNAL decisions the UI makes about a thread so
// they can be (a) shared by the render systems and (b) unit/e2e tested
// headlessly with no window, no UIContext, and no graphics backend. Nothing
// here draws — it only classifies. The sidebar/main-pane systems delegate to
// these so the tested logic IS the shipped logic (no duplicated copy).

#include "../api/types.h"

namespace ecs::model {

// The single notion the UI uses to decide whether a row "shouts". Only the
// Attention state earns a dot+bold in the sidebar.
inline bool is_attention(api::ThreadState s) {
    return s == api::ThreadState::Attention;
}

// Shape-per-status glyph shown at the left of an attention-worthy sidebar row.
// Status is readable by SHAPE, not color alone:
//   Blocked / needs-you  -> Triangle (red,   most urgent)
//   Review (agent-verified) -> Diamond (green)
//   Done                 -> Dot      (blue)
//   working / parked / archived / calm -> None
enum class Glyph { None, Triangle, Diamond, Dot };

// Precedence mirrors the design mock's ordering: blocked, then review, then
// done, then a bare Attention state (waiting-on-you) also earns the triangle.
inline Glyph glyph_for(const api::SessionSummary& s) {
    if (s.tag == api::ThreadTag::Blocked) return Glyph::Triangle;
    if (s.tag == api::ThreadTag::Review) return Glyph::Diamond;
    if (s.tag == api::ThreadTag::Done) return Glyph::Dot;
    if (s.state == api::ThreadState::Attention) return Glyph::Triangle;
    return Glyph::None;
}

// ---- Smart-view membership predicates ----
// Home is a digest (not a simple filter) so it has no single predicate; the
// three filterable smart views do:
inline bool in_blocked_view(const api::SessionSummary& s) {
    return s.tag == api::ThreadTag::Blocked;
}
inline bool in_review_view(const api::SessionSummary& s) {
    return s.state == api::ThreadState::Ready;  // agent-verified
}
inline bool in_starred_view(const api::SessionSummary& s) {
    return s.starred;
}
// Archived-ness as the whole app should ask it: the user's machine-local
// overlay when they have expressed one, the backend's own state otherwise.
inline bool is_archived(const api::SessionSummary& s) {
    return s.archive_override.value_or(s.state == api::ThreadState::Archived);
}
inline bool in_archived_view(const api::SessionSummary& s) {
    return is_archived(s);
}

}  // namespace ecs::model
