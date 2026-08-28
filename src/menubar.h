// menubar.h
// C API for the native macOS menu-bar extra (NSStatusItem). Implemented in
// menubar.mm (Obj-C++). The C++ app calls these from main.cpp; the AppKit
// side lives entirely behind this extern "C" seam, mirroring the pattern used
// by metal_activate_app() in sokol_impl.mm.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Create the NSStatusItem (idempotent — safe to call more than once). Must run
// on the main thread AFTER NSApp exists. NEVER call from the headless path.
void menubar_install(void);

// Update the menu-bar title + the live status row to reflect the blocked
// count. Cheap; safe to call every frame (the .mm no-ops if unchanged).
void menubar_set_blocked(int n);

// Action flags set by the menu items, polled + cleared by the C++ side once
// per frame. Each returns true exactly once per fire, then clears.
bool menubar_take_show(void);
bool menubar_take_new_task(void);
void menubar_refresh_shortcuts(void);
void menubar_set_shortcut_recording(int command);
bool menubar_take_command(int* command);
bool menubar_take_recorded_shortcut(int* key, unsigned char* modifiers);
void menubar_diagnostics(char* out, int cap);
bool menubar_edit_binding(const char* selector, unsigned short* key_code,
                           unsigned long long* modifiers);
void menubar_simulate_command(int command);

#ifdef __cplusplus
}
#endif
