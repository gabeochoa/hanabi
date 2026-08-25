#pragma once

// What a transcript line's draw callbacks need, parked ON the line's entity.
//
// The callbacks used to capture it: the visible text, the find query and a
// shared_ptr holding the element's own id (which is not known until the
// element exists). That is a capture of two strings and a control block --
// well past libc++'s 24-byte std::function buffer, so the function itself was
// a malloc, the strings inside it were two more, and afterhours copies a
// ComponentConfig three times on the way into a widget, cloning all of it each
// time. Nine allocations per visible line per frame, for text the entity is
// already holding.
//
// An immediate-mode widget's ENTITY is the stable thing across frames -- that
// is the whole point of mk() -- so the state lives here and the callbacks
// capture one pointer to it, which fits the inline buffer and makes every
// clone free. Assigning into the existing strings reuses their capacity, so a
// steady frame allocates nothing at all.
//
// IT IS ITS OWN HEADER RATHER THAN A MEMBER OF components.h, and that is not
// tidiness. It needs ../ui/link_detect.h for the link runs, and components.h
// is included by the headless unit tests -- which then pull in
// find_highlight.h and text_select.h and fail to compile against a
// `draw_rectangle_rounded` the test build resolves to a different overload.
// The main binary built clean and three test binaries did not.

#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../../vendor/afterhours/src/core/entity.h"
#include "../ui/link_detect.h"

namespace ecs {

struct LineDrawState : public afterhours::BaseComponent {
    std::string text;
    std::string query;
    std::vector<hanabi::links::Link> links;
    afterhours::EntityID id = -1;
};

}  // namespace ecs
