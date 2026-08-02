// native_extras.mm
// Native macOS integrations (global hotkey, notifications, Spotlight seam)
// behind the extern "C" seam declared in native_extras.h. Same pattern as
// menubar.mm and metal_activate_app() in sokol_impl.mm: AppKit / Carbon /
// UserNotifications live here; the C++ core only ever calls C functions and
// polls one-shot atomic "take" flags each frame. No Obj-C type crosses over.
//
// Threading: install/notify run on the main thread (the frame loop is on the
// main thread). The Carbon hotkey callback runs on the main run loop too, so
// it only touches a std::atomic flag — no ECS mutation from the callback.

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>   // RegisterEventHotKey, kVK_ANSI_N, event handler
#include <atomic>
#include <cstring>

#include "native_extras.h"

// ===========================================================================
// 1. Global hotkey — Cmd+Shift+N (see the CHOSEN CHORD note in native_extras.h)
// ===========================================================================
//
// Carbon's RegisterEventHotKey is the long-standing way to get a truly global
// hotkey from a normal (non-accessibility, non-bundled) app: it does not need
// the Accessibility permission that an NSEvent global monitor does for key
// events, and it works from a bare executable. It is "legacy" Carbon but not
// deprecated for this use, and there is no modern Cocoa replacement that gives
// a system-wide hotkey without extra entitlements — so it remains the right
// tool here. We register a single hotkey and install one process-lifetime
// application event handler that flips an atomic the frame loop drains.

static std::atomic<bool> g_hotkey_triggered{false};
static EventHotKeyRef g_hotkey_ref = nullptr;   // non-null once registered
static bool g_hotkey_installed = false;

// Our hotkey's identity, matched in the handler.
static const OSType kHotkeySig = 'hnbi';        // 4-char creator-style tag
static const UInt32 kHotkeyId = 1;

static OSStatus hotkey_handler(EventHandlerCallRef nextHandler,
                               EventRef event, void* userData) {
    (void)nextHandler;
    (void)userData;
    EventHotKeyID hkId;
    OSStatus st = GetEventParameter(event, kEventParamDirectObject,
                                    typeEventHotKeyID, nullptr, sizeof(hkId),
                                    nullptr, &hkId);
    if (st == noErr && hkId.signature == kHotkeySig && hkId.id == kHotkeyId) {
        // Set the one-shot; the C++ frame loop does the actual activate +
        // new-task on the main thread. Keep this callback trivial.
        g_hotkey_triggered.store(true);
    }
    return noErr;
}

void native_hotkey_install(void) {
    // Idempotent: only register once.
    if (g_hotkey_installed) return;
    // NSApp must exist for the event target to be live; guard defensively so a
    // mis-timed call is a harmless no-op and the caller can retry next frame.
    if (NSApp == nil) return;

    EventTypeSpec evtSpec;
    evtSpec.eventClass = kEventClassKeyboard;
    evtSpec.eventKind = kEventHotKeyPressed;

    // Install the application-level handler (once).
    OSStatus st = InstallApplicationEventHandler(&hotkey_handler, 1, &evtSpec,
                                                 nullptr, nullptr);
    if (st != noErr) {
        NSLog(@"native_extras: InstallApplicationEventHandler failed (%d)",
              (int)st);
        return;
    }

    EventHotKeyID hkId;
    hkId.signature = kHotkeySig;
    hkId.id = kHotkeyId;

    // Cmd+Shift+N. kVK_ANSI_N is the physical N key; cmdKey|shiftKey are the
    // Carbon modifier masks.
    st = RegisterEventHotKey(kVK_ANSI_N, cmdKey | shiftKey, hkId,
                             GetApplicationEventTarget(), 0, &g_hotkey_ref);
    if (st != noErr) {
        // A collision with another app's global hotkey lands here. Log and
        // carry on — the app is fully usable without the chord.
        NSLog(@"native_extras: RegisterEventHotKey(Cmd+Shift+N) failed (%d) — "
              @"another app may own this chord",
              (int)st);
        g_hotkey_ref = nullptr;
        return;
    }

    g_hotkey_installed = true;
    NSLog(@"native_extras: global hotkey Cmd+Shift+N installed");
}

bool native_hotkey_take_triggered(void) {
    return g_hotkey_triggered.exchange(false);
}

// ===========================================================================
// 2. Native notification
// ===========================================================================
//
// API choice: NSUserNotification (the older NSUserNotificationCenter API)
// rather than the modern UNUserNotificationCenter.
//
//   Why: UNUserNotificationCenter REQUIRES the app to (a) run from a proper
//   .app bundle with a bundle identifier and (b) request authorization, which
//   would pop a permission prompt. hanabi builds as a bare output/hanabi.exe
//   (no bundle id) — UNUserNotificationCenter simply refuses to deliver
//   without a bundle, and asking for authorization from a non-bundled binary
//   is undefined. NSUserNotification is deprecated BUT needs zero permission
//   and no bundle plumbing, which is exactly right for a dev tool that ships as
//   a plain executable. Tradeoff: it's deprecated (may be removed in a future
//   macOS) and, from a non-bundled binary, delivery is best-effort — the OS
//   may drop the banner but the call is always safe and never prompts. We
//   accept that: no-prompt + no-bundle-requirement matters more here than the
//   deprecation, and #3 documents the bundle gap that would also unlock UN.
//
// The deprecation warnings are silenced narrowly around this call so the .mm
// still compiles clean under -Wall -Wextra (the whole file is built with those).

void native_notify(const char* title, const char* body) {
    if (title == nullptr || title[0] == '\0') return;
    @autoreleasepool {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSUserNotification* note = [[[NSUserNotification alloc] init]
            autorelease];
        note.title = [NSString stringWithUTF8String:title];
        if (body != nullptr && body[0] != '\0')
            note.informativeText = [NSString stringWithUTF8String:body];
        note.soundName = NSUserNotificationDefaultSoundName;
        [[NSUserNotificationCenter defaultUserNotificationCenter]
            deliverNotification:note];
#pragma clang diagnostic pop
    }
}

// ===========================================================================
// 3. Spotlight / CoreSpotlight — HONEST FEASIBILITY: seam-only NO-OP here
// ===========================================================================
//
// FEASIBILITY VERDICT: NOT viable in the current build. CoreSpotlight's
// CSSearchableIndex indexes items on behalf of an app identified by its bundle
// identifier; the index is keyed to that bundle and surfaced by Spotlight only
// for apps registered with LaunchServices (i.e. a real Foo.app bundle). hanabi
// ships as a bare output/hanabi.exe with NO bundle identifier and is not
// LaunchServices-registered, so CSSearchableIndex has no owning app to attach
// results to — indexed items would either be rejected or never appear in
// system search, and tapping a result could not deep-link back (no bundle to
// re-launch). Implementing real indexing now would be faking a capability the
// build cannot deliver.
//
// So this is a documented NO-OP seam: the C entry point exists (so main.cpp
// can call it once threads are known and the wiring is proven), but it does
// nothing beyond a one-time log. To actually ship Spotlight, hanabi must first
// be packaged as a .app bundle with an Info.plist bundle identifier and be
// LaunchServices-registered; then this function would build a CSSearchableItem
// (uniqueIdentifier = thread id, title = thread title) and add it to
// [CSSearchableIndex defaultSearchableIndex]. Tracked as a gap.

void native_spotlight_index(const char* id, const char* title) {
    (void)id;
    (void)title;
    // NO-OP: needs a .app bundle + bundle identifier (see the note above).
    // Log exactly once so the gap is visible in a windowed run without spamming.
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        NSLog(@"native_extras: Spotlight indexing is a no-op in the "
              @"non-bundled build (needs a .app bundle + bundle id)");
    }
}
