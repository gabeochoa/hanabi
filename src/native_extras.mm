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
#import <CoreSpotlight/CoreSpotlight.h>          // CSSearchableIndex/Item (bundled Spotlight)
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>  // UTTypeText
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
// tool here. We install one process-lifetime application event handler that
// flips an atomic the frame loop drains.
//
// FOCUS GATING (bug fix — user reported the chord blocking Chrome):
//   Cmd+Shift+N is Chrome's "New Incognito Window". A Carbon RegisterEventHotKey
//   registration is SYSTEM-WIDE and CONSUMES the chord even when hanabi is not
//   frontmost — so while it was registered for the whole process lifetime, it
//   silently ate Cmd+Shift+N in Chrome (and every other app). Merely ignoring
//   the press inside the handler does NOT help: the event is already swallowed
//   before it reaches the frontmost app.
//
//   The only real fix is to REGISTER the Carbon hotkey ONLY while hanabi is the
//   active (frontmost) app, and UNREGISTER it the moment hanabi resigns active.
//   We do that by observing NSApplication's didBecomeActive / willResignActive
//   notifications (see HotkeyFocusObserver below). Result:
//     - hanabi focused  -> hotkey registered -> Cmd+Shift+N summons/new-task.
//     - hanabi NOT focused -> hotkey unregistered -> Chrome (and everything
//       else) receives Cmd+Shift+N normally.
//   This trades away "summon from any app" for not stealing a common chord
//   system-wide, which is the user's stated intent ("only when focused").

static std::atomic<bool> g_hotkey_triggered{false};
static EventHotKeyRef g_hotkey_ref = nullptr;   // non-null while registered
static bool g_handler_installed = false;        // Carbon event handler once
static bool g_focus_observed = false;           // NSApp notifications hooked

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

// Register the Carbon hotkey iff not already registered. Called when hanabi
// becomes active. Runs on the main thread (notification + first-frame path).
static void hotkey_register(void) {
    if (g_hotkey_ref != nullptr) return;   // already registered

    EventHotKeyID hkId;
    hkId.signature = kHotkeySig;
    hkId.id = kHotkeyId;

    // Cmd+Shift+N. kVK_ANSI_N is the physical N key; cmdKey|shiftKey are the
    // Carbon modifier masks.
    OSStatus st = RegisterEventHotKey(kVK_ANSI_N, cmdKey | shiftKey, hkId,
                                      GetApplicationEventTarget(), 0,
                                      &g_hotkey_ref);
    if (st != noErr) {
        // A collision with another app's global hotkey lands here. Log and
        // carry on — the app is fully usable without the chord.
        NSLog(@"native_extras: RegisterEventHotKey(Cmd+Shift+N) failed (%d) — "
              @"another app may own this chord",
              (int)st);
        g_hotkey_ref = nullptr;
        return;
    }
    NSLog(@"native_extras: global hotkey Cmd+Shift+N registered (hanabi active)");
}

// Unregister the Carbon hotkey iff registered. Called when hanabi resigns
// active, so the chord flows through to whatever app is now frontmost.
static void hotkey_unregister(void) {
    if (g_hotkey_ref == nullptr) return;   // nothing registered
    OSStatus st = UnregisterEventHotKey(g_hotkey_ref);
    if (st != noErr) {
        NSLog(@"native_extras: UnregisterEventHotKey failed (%d)", (int)st);
    }
    g_hotkey_ref = nullptr;
    // Clear any press that arrived right at the focus boundary so a stale
    // trigger doesn't fire after we've decided hanabi isn't focused.
    g_hotkey_triggered.store(false);
    NSLog(@"native_extras: global hotkey Cmd+Shift+N unregistered (hanabi "
          @"resigned active) — passes through to other apps");
}

// Observer that toggles the Carbon hotkey registration with hanabi's active
// state. Lives for the process lifetime (never released after install).
@interface HotkeyFocusObserver : NSObject
@end

@implementation HotkeyFocusObserver
- (void)appDidBecomeActive:(NSNotification*)note {
    (void)note;
    hotkey_register();
}
- (void)appWillResignActive:(NSNotification*)note {
    (void)note;
    hotkey_unregister();
}
@end

static HotkeyFocusObserver* g_focus_observer = nil;

void native_hotkey_install(void) {
    // NSApp must exist for the event target + notifications to be live; guard
    // defensively so a mis-timed call is a harmless no-op and the caller can
    // retry next frame.
    if (NSApp == nil) return;

    // Install the application-level Carbon handler exactly once (it stays for
    // the process lifetime; registration/unregistration of the hotkey itself
    // is what toggles with focus).
    if (!g_handler_installed) {
        EventTypeSpec evtSpec;
        evtSpec.eventClass = kEventClassKeyboard;
        evtSpec.eventKind = kEventHotKeyPressed;
        OSStatus st = InstallApplicationEventHandler(&hotkey_handler, 1,
                                                     &evtSpec, nullptr, nullptr);
        if (st != noErr) {
            NSLog(@"native_extras: InstallApplicationEventHandler failed (%d)",
                  (int)st);
            return;
        }
        g_handler_installed = true;
    }

    // Hook NSApplication active/resign notifications exactly once so the
    // hotkey is only registered while hanabi is frontmost.
    if (!g_focus_observed) {
        g_focus_observer = [[HotkeyFocusObserver alloc] init];
        NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:g_focus_observer
               selector:@selector(appDidBecomeActive:)
                   name:NSApplicationDidBecomeActiveNotification
                 object:nil];
        [nc addObserver:g_focus_observer
               selector:@selector(appWillResignActive:)
                   name:NSApplicationWillResignActiveNotification
                 object:nil];
        g_focus_observed = true;
        NSLog(@"native_extras: hotkey focus-gating installed (Cmd+Shift+N "
              @"active only while hanabi is frontmost)");
    }

    // Seed the initial state from whether hanabi is CURRENTLY active. On the
    // first windowed frame the app is normally frontmost, so this registers
    // immediately; if it isn't, didBecomeActive will register on next focus.
    if ([NSApp isActive]) {
        hotkey_register();
    }
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
// 3. Spotlight / CoreSpotlight — REAL when bundled, safe NO-OP otherwise
// ===========================================================================
//
// CoreSpotlight's CSSearchableIndex indexes items on behalf of an app
// identified by its bundle identifier; the index is keyed to that bundle and
// surfaced by Spotlight only for apps registered with LaunchServices (a real
// Foo.app bundle). We now SHIP a .app bundle (make bundle -> Hanabi.app with
// CFBundleIdentifier com.hanabi.app, LaunchServices-registered), so real
// indexing is viable — BUT ONLY when running from inside that bundle.
//
// GATING: we detect a real bundle at runtime via
// NSBundle.mainBundle.bundleIdentifier. A bare dev binary (output/hanabi.exe)
// has a nil bundle identifier — for it we keep the historical safe no-op
// (indexing from a non-bundled binary is rejected / never surfaces and can't
// deep-link back). This keeps the dev workflow untouched while the bundled
// app gets real Spotlight entries. Thread deep-link (tapping a result) still
// needs an NSUserActivity/URL-scheme handler in the bundle — tracked as a
// follow-up; indexing itself is the first, independently-useful half.

void native_spotlight_index(const char* id, const char* title) {
    if (id == nullptr || title == nullptr) return;
    @autoreleasepool {
        // Only index when we're a real, identified bundle. A nil identifier
        // means the bare dev binary — stay a safe no-op there.
        if ([[NSBundle mainBundle] bundleIdentifier] == nil) {
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) {
                NSLog(@"native_extras: Spotlight indexing is a no-op in the "
                      @"non-bundled dev binary (run from Hanabi.app to index)");
            }
            return;
        }
        NSString* nsId = [NSString stringWithUTF8String:id];
        NSString* nsTitle = [NSString stringWithUTF8String:title];
        if (nsId.length == 0) return;

        CSSearchableItemAttributeSet* attrs =
            [[CSSearchableItemAttributeSet alloc]
                initWithContentType:UTTypeText];
        attrs.title = nsTitle;
        attrs.contentDescription = @"hanabi thread";

        CSSearchableItem* item =
            [[CSSearchableItem alloc] initWithUniqueIdentifier:nsId
                                              domainIdentifier:@"threads"
                                                  attributeSet:attrs];
        [[CSSearchableIndex defaultSearchableIndex]
            indexSearchableItems:@[ item ]
               completionHandler:^(NSError* _Nullable error) {
                   if (error != nil) {
                       NSLog(@"native_extras: Spotlight index failed: %@",
                             error.localizedDescription);
                   } else {
                       static std::atomic<bool> okLogged{false};
                       if (!okLogged.exchange(true)) {
                           NSLog(@"native_extras: Spotlight index OK "
                                 @"(first item donated to CSSearchableIndex)");
                       }
                   }
               }];
    }
}
