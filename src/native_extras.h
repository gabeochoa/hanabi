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
//      you" count newly increases, so the user is told a thread needs them
//      even when hanabi is in the background. Uses NSUserNotification (see the
//      API note in native_extras.mm for the deprecated-vs-permission tradeoff).
//
//   3. Spotlight indexing seam. native_spotlight_index() exists so open/recent
//      threads COULD be indexed into system search — but see the honest
//      feasibility note in native_extras.mm: CoreSpotlight needs a real .app
//      bundle + bundle identifier, which the bare output/hanabi.exe build does
//      not have, so this is a documented NO-OP in the current build.
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

// Register the system-wide hotkey (Cmd+Shift+N). Idempotent — safe to call
// more than once (only the first call installs). Must run on the main thread
// after NSApp exists. NEVER call from the headless path: it installs a
// process-lifetime Carbon event handler + a global hotkey registration.
void native_hotkey_install(void);

// One-shot: returns true exactly once per hotkey press, then clears. Polled by
// the C++ frame loop, which then runs the existing activate + new-task path.
bool native_hotkey_take_triggered(void);

// ---- 2. Native notification ------------------------------------------------

// Post a native macOS notification. title/body are UTF-8 C strings (never
// retained). No-op if title is null/empty. Windowed path only — the caller
// must NOT call this from the headless --screenshot path (it would request
// notification delivery / could prompt). Rate-limiting/debounce is the
// caller's responsibility (see main.cpp's blocked-count seam).
void native_notify(const char* title, const char* body);

// ---- 3. Spotlight (best-effort; NO-OP in the non-bundled build) ------------

// Would index a thread (id + title) into CoreSpotlight so system search can
// find it. In the current bare-executable build this is a documented NO-OP:
// CSSearchableIndex requires the app to run from a real .app bundle with a
// bundle identifier registered with LaunchServices, which output/hanabi.exe is
// not. The seam exists so the wiring is ready once hanabi ships as a bundle.
// See the feasibility note in native_extras.mm.
void native_spotlight_index(const char* id, const char* title);

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

#ifdef __cplusplus
}
#endif
