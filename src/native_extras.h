// native_extras.h
// C API for the remaining native macOS integrations that extend Phase G
// beyond the menu-bar extra (menubar.h). Implemented in native_extras.mm
// (Obj-C++). The C++ app calls these from main.cpp; every AppKit / Carbon /
// UserNotifications type lives entirely behind this extern "C" seam, mirroring
// menubar.h and metal_activate_app() in sokol_impl.mm. No Obj-C type ever
// crosses into the C++ core.
//
// Three features live here:
//
//   1. Focus-gated hotkey. A chord (Cmd+Shift+N) that raises hanabi (and starts
//      a new task) — registered via Carbon RegisterEventHotKey ONLY while
//      hanabi is the frontmost app. Because RegisterEventHotKey consumes a
//      chord system-wide for the process lifetime, and Cmd+Shift+N is also
//      Chrome's "New Incognito Window", a permanent global registration would
//      steal it from every other app. So native_extras.mm observes
//      NSApplication become/resign-active and registers the hotkey on focus,
//      unregisters on blur — the chord works in hanabi and passes through to
//      whatever app is focused otherwise. See the CHOSEN CHORD note below.
//
//   2. Native notification. Posts a macOS notification when the "blocked on
//      you" count newly increases. The bundled app uses
//      UNUserNotificationCenter, requests permission on its first windowed run,
//      and routes a click back to the notification's thread.
//
//   3b. Image attachments (section 6 below). Pasting or dropping an image is
//      an AppKit fact — NSPasteboard holds the pixels, NSDraggingDestination
//      delivers the file — so both arrive through this seam as a PATH the
//      immediate-mode core can hold in app state and draw from.
//
//   3. Spotlight catalog sync. The bundled app indexes the current bounded
//      session catalog with title, preview, and hanabi:// deep link metadata,
//      updates changed rows, and removes identifiers that left the catalog. The
//      bare developer executable stays a safe no-op.
//
// -------------------------------------------------------------------------
// CHOSEN GLOBAL HOTKEY CHORD:  Cmd + Shift + N
//   Cmd(⌘) + Shift(⇧) + N — "N" for "new". Brings hanabi to the front and
//   starts a new task. Chosen over Cmd+Shift+Space (which collides with
//   several launchers/input-source switchers on many setups). Registered ONLY
//   on the windowed run path; the headless --screenshot path never installs
//   it, so no global listener lingers after a capture.
// -------------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ---- 1. Global hotkey ------------------------------------------------------

// Register the system-wide hotkeys (Cmd+Shift+N new task, Cmd+Shift+K
// palette). Idempotent — safe to call
// more than once (only the first call installs). Must run on the main thread
// after NSApp exists. NEVER call from the headless path: it installs a
// process-lifetime Carbon event handler + a global hotkey registration.
void native_hotkey_install(void);

// One-shot: returns true exactly once per hotkey press, then clears. Polled by
// the C++ frame loop, which then runs the existing activate + new-task path.
bool native_hotkey_take_triggered(void);

// The same, for Cmd+Shift+K: bring hanabi forward and open the command
// palette. Registered and unregistered with the chord above, so an unfocused
// hanabi never swallows a chord another app owns.
bool native_palette_hotkey_take_triggered(void);

// ---- 2. Native notification ------------------------------------------------

// Installs the UNUserNotificationCenter delegate and requests alert/sound
// authorization once for a windowed bundled launch. A bare developer executable
// remains a no-op. The headless paths never call this function.
void native_notifications_start(void);

// Queues a modern macOS notification. A first notification waits for the
// authorization answer instead of being dropped. `sound` controls whether this
// request carries UNNotificationSound.defaultSound. Clicking it routes
// `thread_id` through native_take_open_thread().
void native_notify(const char* title, const char* body, const char* thread_id,
                   bool sound);

// ---- 3. Spotlight ----------------------------------------------------------

typedef struct NativeSpotlightItem {
    const char* id;
    const char* title;
    const char* preview;
    const char* url;
} NativeSpotlightItem;

// Reconciles the app's complete bounded catalog. Existing identifiers update in
// place, missing identifiers are deleted, and no network is touched. A bare
// developer executable remains a no-op.
void native_spotlight_sync(const NativeSpotlightItem* items, int count);

// Copies a one-line bundle / notification / Spotlight status into `out`.
void native_integration_status(char* out, int cap);

// Test-only process-local seam for the notification click route. It performs no
// AppKit, notification, network, or filesystem work.
void native_simulate_notification_click(const char* thread_id);
void native_simulate_open_url(const char* url);

// ---- 4. Spotlight deep-link (open a thread from a tapped result) -----------

// Install the URL / Apple-event handler so a `hanabi://thread/<id>` open —
// e.g. from tapping a CoreSpotlight result whose contentURL uses that scheme —
// is captured. Idempotent; main-thread + NSApp required; windowed path only.
void native_openurl_install(void);

// One-shot: if a `hanabi://thread/<id>` open arrived since the last call,
// writes the thread id into `out` (UTF-8, NUL-terminated, up to cap-1 bytes)
// and returns true, then clears. Returns false (leaving out untouched) when
// nothing is pending. Polled by the C++ frame loop, which then sets
// AppComponent::requestOpenTab to open + navigate to the thread.
bool native_take_open_thread(char* out, int cap);

// ---- 6. Image attachments: clipboard paste + file drop ---------------------
//
// Both answer with a filesystem PATH to an image, never with bytes: the
// transcript's inline-image cache (src/ui/inline_image.h) already turns a path
// into a texture, so a path is the one currency both the chip thumbnail and any
// future send path can spend. AppKit owns the pixels only long enough to write
// them somewhere the C++ side can read.
//
// The two halves poll differently, and deliberately:
//   * A PASTE is a pull. The chord (Cmd+V) is a C++ key read, and the
//     pasteboard can be asked what it holds at that instant, so there is
//     nothing to latch — native_take_clipboard_image() answers the question
//     the keystroke asked.
//   * A DROP is a push. AppKit delivers it whenever the user lets go of the
//     mouse, so the .mm latches the path and the frame loop drains it, exactly
//     as the hotkey and the deep-link do.

// True when the clipboard holds an image RIGHT NOW: writes a path to it into
// `out` (UTF-8, NUL-terminated, up to cap-1 bytes) and returns true, else
// returns false leaving `out` untouched. Copied-in-Finder image FILES answer
// with their own path; raw image data (a screenshot, an image copied from a
// browser) is written to a temp PNG that outlives the call. Safe to call every
// frame, but the caller should only ask on the paste chord — reading the
// pasteboard allocates.
bool native_take_clipboard_image(char* out, int cap);

// Register the app window as a drag destination for image files. Idempotent;
// main-thread + an existing window required, so the frame loop installs it on
// the first windowed frame alongside the hotkey. NEVER call from the headless
// path. No-op (with a log) if the window is not up yet — call again next frame.
void native_filedrop_install(void);

// One-shot: if an image file was dropped on the window since the last call,
// writes its path into `out` (UTF-8, NUL-terminated, up to cap-1 bytes) and
// returns true, then clears. Returns false (leaving out untouched) when
// nothing is pending. Drops arrive one path per call — a multi-file drop
// queues them, so a caller that keeps asking until it answers false drains the
// whole drop.
bool native_dropped_image_pending(void);
bool native_take_dropped_image(char* out, int cap);

// TEST SEAM. Pushes `path` into the same pending-drop queue a real AppKit drop
// feeds, so the frame-loop drain + everything downstream of it can be exercised
// without a hand on a mouse: a drag is not in the widget tree and the scripted
// harness cannot produce one. main.cpp calls this once when HANABI_DROP_TEST is
// set. It does NOT simulate AppKit's delivery — that half stays manual.
void native_simulate_file_drop(const char* path);
// ---- 6. Native file picker (NSOpenPanel) -----------------------------------
//
// One modal ask: "which folder?". Blocks until the user answers — the frame
// loop stops for as long as the panel is up, which is what a modal panel means
// and what every native app does with one. Main thread only; NEVER call from
// the headless path (nothing would be there to dismiss it).
//
// Directories, not files, because the one place hanabi has to ask is where the
// owned Markdown export writes. The FILE half of this seam — the picker the
// breakdown wants for the `file_upload` tool — is deliberately not here: a
// picked file would have nowhere to go. hanabi's api::Client has no notion of
// a tool asking for anything, and the backend's file answer is a durable
// upload HANDLE (a file id the client mints by uploading first), not bytes, so
// a picker wired to it today would hand back a file that could never arrive.
// It goes in the day there is an upload path to give it.
//
// `prompt` is the panel's action-button text ("Choose"), or null for the
// system default. Returns true and writes the chosen absolute path into `out`
// (UTF-8, NUL-terminated, up to cap-1 bytes); returns false when the user
// cancels, leaving `out` untouched.
bool native_pick_directory(const char* prompt, char* out, int cap);
// ---- 4b. Open a URL in the user's browser ----------------------------------

// Hand `url` (UTF-8, http/https) to the system's default handler. Used by the
// transcript when a work-tracker id is clicked. No-op on a null/empty string
// or a scheme this does not recognise. NEVER call it from the headless
// capture or the scripted-UI harness — a render with nobody in front of it has
// no browser to give (hanabi::links::headless() gates that call site).
void native_open_url(const char* url);

// ---- 5. OS appearance (for the "System" theme choice) ----------------------

// True when the OS is in Dark appearance (macOS: AppleInterfaceStyle == Dark).
// Defined in sokol_impl.mm (macOS-only link). Lets the "System" theme resolve
// to the real OS setting instead of always falling back to Dark (gap #16).
bool macos_is_dark_mode(void);

typedef struct NativeFontFace {
    char family[32];
    char label[64];
    char weight[16];
    char path[1024];
    float point_scale;
} NativeFontFace;

int native_font_faces(NativeFontFace* out, int cap);

#ifdef __cplusplus
}

namespace hanabi {
// Cross-platform wrapper: is the OS in dark mode? macOS uses the real setting;
// other platforms default to dark (no OS seam wired). Header-only so any UI
// code can call it without linking a new TU.
inline bool os_is_dark_mode() {
#if defined(__APPLE__)
    return macos_is_dark_mode();
#else
    return true;
#endif
}
}  // namespace hanabi
#endif
