#pragma once

#include "menubar.h"
#include "shortcuts.h"

namespace hanabi::palette_rows {

inline bool lists(const hanabi::shortcuts::Definition& item) {
    return item.in_palette && menubar_command_enabled(static_cast<int>(item.command));
}

}
