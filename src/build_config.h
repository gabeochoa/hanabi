#pragma once

#define AFTER_HOURS_UI_SINGLE_COLLECTION
#define AFTER_HOURS_USE_METAL
#define AFTERHOURS_DEFAULT_TEXT_INSET 5.f
// afterhours clamps every resolved font size up to this floor
// (plugins/ui/rendering.h), and upstream 0248b9f raised its default from 10 to
// 16 to match the accessibility minimum the library declares. Hanabi's type
// scale is 9pt..20pt (src/ui/theme.h `namespace type`), so a 16 floor silently
// redraws everything below LG at 16px inside boxes hanabi measured at the real
// size: overlapping composer chrome, a clipped status bar, un-ellipsised
// sidebar rows. 10 is the floor hanabi already rendered against, so this keeps
// the pin bump pixel-neutral. The accessibility signal is kept rather than
// dropped: Theme::min_font_size_warn_720p reports a request under the floor
// instead of silently resizing it.
#define AFTERHOURS_MIN_FONT_SIZE 10.0f
#define HANABI_GPU_ACCOUNTING
#define FMT_HEADER_ONLY
