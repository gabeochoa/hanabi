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
#include <mutex>
#include <string>
#include <vector>

#include "native_extras.h"

// Debug-only native logging. The hotkey register/unregister fires on EVERY
// focus change (app activate/resign), which floods the console (Gabe: "you can
// turn off this logging, it's working as expected"). Gate the chatty ones
// behind HANABI_NATIVE_LOG=1 so they're SILENT by default; one-shot install /
// real-error logs stay unconditional. Evaluated once (env is process-static).
static bool hanabi_native_log_enabled(void) {
    static const bool on = [] {
        const char* v = getenv("HANABI_NATIVE_LOG");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}
#define HLOG(...)                                                              \
    do {                                                                       \
        if (hanabi_native_log_enabled()) NSLog(__VA_ARGS__);                   \
    } while (0)

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
static std::atomic<bool> g_palette_triggered{false};
static EventHotKeyRef g_hotkey_ref = nullptr;   // non-null while registered
static EventHotKeyRef g_palette_ref = nullptr;  // Cmd+Shift+K, same lifetime
static bool g_handler_installed = false;        // Carbon event handler once
static bool g_focus_observed = false;           // NSApp notifications hooked

// Our hotkey's identity, matched in the handler.
static const OSType kHotkeySig = 'hnbi';        // 4-char creator-style tag
static const UInt32 kHotkeyId = 1;
static const UInt32 kPaletteHotkeyId = 2;

static OSStatus hotkey_handler(EventHandlerCallRef nextHandler,
                               EventRef event, void* userData) {
    (void)nextHandler;
    (void)userData;
    EventHotKeyID hkId;
    OSStatus st = GetEventParameter(event, kEventParamDirectObject,
                                    typeEventHotKeyID, nullptr, sizeof(hkId),
                                    nullptr, &hkId);
    if (st == noErr && hkId.signature == kHotkeySig) {
        // Set the one-shot; the C++ frame loop does the actual activate +
        // new-task (or palette) on the main thread. Keep this callback trivial.
        if (hkId.id == kHotkeyId) g_hotkey_triggered.store(true);
        else if (hkId.id == kPaletteHotkeyId) g_palette_triggered.store(true);
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
    HLOG(@"native_extras: global hotkey Cmd+Shift+N registered (hanabi active)");

    // Cmd+Shift+K opens the command palette. Same focus-gated lifetime as the
    // chord above, for the same reason: an unfocused app must not swallow a
    // chord another app owns. A failure here is not fatal — Cmd+K still works
    // inside the app; only the summon-from-elsewhere version is lost.
    EventHotKeyID palId;
    palId.signature = kHotkeySig;
    palId.id = kPaletteHotkeyId;
    OSStatus pst = RegisterEventHotKey(kVK_ANSI_K, cmdKey | shiftKey, palId,
                                       GetApplicationEventTarget(), 0,
                                       &g_palette_ref);
    if (pst != noErr) {
        NSLog(@"native_extras: RegisterEventHotKey(Cmd+Shift+K) failed (%d) — "
              @"another app may own this chord",
              (int)pst);
        g_palette_ref = nullptr;
    }
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
    if (g_palette_ref != nullptr) {
        UnregisterEventHotKey(g_palette_ref);
        g_palette_ref = nullptr;
    }
    // Clear any press that arrived right at the focus boundary so a stale
    // trigger doesn't fire after we've decided hanabi isn't focused.
    g_hotkey_triggered.store(false);
    g_palette_triggered.store(false);
    HLOG(@"native_extras: global hotkey Cmd+Shift+N unregistered (hanabi "
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

bool native_palette_hotkey_take_triggered(void) {
    return g_palette_triggered.exchange(false);
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

// Forward decl: the shared "a thread wants opening" setter (defined with the
// deep-link slot at the bottom). Both the notification-click delegate and the
// hanabi:// URL handler feed the SAME pending slot that the frame loop drains.
static void set_pending_open_thread(const std::string& id);

// Delegate for NSUserNotificationCenter: a delivered banner is shown even when
// hanabi is frontmost, and CLICKING it activates hanabi + opens the thread
// carried in the notification's userInfo["thread_id"].
@interface HanabiNotifDelegate : NSObject <NSUserNotificationCenterDelegate>
@end
@implementation HanabiNotifDelegate
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
- (BOOL)userNotificationCenter:(NSUserNotificationCenter*)center
     shouldPresentNotification:(NSUserNotification*)notification {
    (void)center; (void)notification;
    return YES;   // present even if hanabi is the active app
}
- (void)userNotificationCenter:(NSUserNotificationCenter*)center
       didActivateNotification:(NSUserNotification*)notification {
    (void)center;
    id tid = notification.userInfo[@"thread_id"];
    if ([tid isKindOfClass:[NSString class]] && [tid length] > 0) {
        set_pending_open_thread(std::string([tid UTF8String]));
        NSLog(@"native_extras: notification click -> open thread id=%@", tid);
        [NSApp activateIgnoringOtherApps:YES];
    }
}
#pragma clang diagnostic pop
@end

static HanabiNotifDelegate* g_notif_delegate = nil;

void native_notify(const char* title, const char* body, const char* thread_id) {
    if (title == nullptr || title[0] == '\0') return;
    @autoreleasepool {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSUserNotificationCenter* center =
            [NSUserNotificationCenter defaultUserNotificationCenter];
        // Install the delegate once so notification clicks open the thread.
        if (g_notif_delegate == nil) {
            g_notif_delegate = [[HanabiNotifDelegate alloc] init];
            center.delegate = g_notif_delegate;
        }
        NSUserNotification* note = [[[NSUserNotification alloc] init]
            autorelease];
        note.title = [NSString stringWithUTF8String:title];
        if (body != nullptr && body[0] != '\0')
            note.informativeText = [NSString stringWithUTF8String:body];
        note.soundName = NSUserNotificationDefaultSoundName;
        // Carry the thread id so a click can deep-link to it (see delegate).
        if (thread_id != nullptr && thread_id[0] != '\0')
            note.userInfo = @{ @"thread_id"
                               : [NSString stringWithUTF8String:thread_id] };
        [center deliverNotification:note];
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
        // Deep-link back: tapping the result opens hanabi://thread/<id>, which
        // our Apple-event handler (native_openurl_install) captures and turns
        // into an open-thread request. The scheme is declared in the bundle's
        // Info.plist (CFBundleURLTypes).
        attrs.contentURL = [NSURL URLWithString:
            [NSString stringWithFormat:@"hanabi://thread/%@",
                [nsId stringByAddingPercentEncodingWithAllowedCharacters:
                    [NSCharacterSet URLPathAllowedCharacterSet]]]];

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

// ===========================================================================
// 4. Spotlight deep-link — open a thread from a hanabi://thread/<id> URL
// ===========================================================================
//
// Tapping a CoreSpotlight result (or any hanabi://thread/<id> open) is
// delivered to a non-App-Store app via the classic Apple-event route:
// kInternetEventClass / kAEGetURL. We register a handler on the shared
// NSAppleEventManager, parse the thread id out of the URL, and stash it behind
// a mutex-guarded pending slot. The C++ frame loop drains it via
// native_take_open_thread() and sets AppComponent::requestOpenTab.
//
// This is the read/handle half; the app must also DECLARE the scheme in its
// Info.plist (CFBundleURLTypes -> hanabi) so LaunchServices routes the URL
// here — done in the `bundle` target. Works from the bundle; harmless (just
// never fires) for the bare dev binary since nothing registers the scheme.

static std::mutex g_open_thread_mu;
static std::string g_pending_open_thread;   // guarded by g_open_thread_mu

// Shared setter: both the hanabi:// URL handler and the notification-click
// delegate feed this one slot; the frame loop drains it. Last write wins (a
// user can only look at one thread at a time; the most recent intent is right).
static void set_pending_open_thread(const std::string& id) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lk(g_open_thread_mu);
    g_pending_open_thread = id;
}

// Parse the thread id from hanabi://thread/<id> (also tolerates
// hanabi:thread/<id> and a trailing slash/query). Returns "" if not a match.
static std::string parse_thread_url(NSString* url) {
    if (url == nil) return std::string();
    std::string s([url UTF8String] ? [url UTF8String] : "");
    // Accept both "hanabi://thread/" and "hanabi:thread/" prefixes.
    static const char* kPfx1 = "hanabi://thread/";
    static const char* kPfx2 = "hanabi:thread/";
    std::string id;
    if (s.rfind(kPfx1, 0) == 0)
        id = s.substr(std::strlen(kPfx1));
    else if (s.rfind(kPfx2, 0) == 0)
        id = s.substr(std::strlen(kPfx2));
    else
        return std::string();
    // Trim a trailing '/' and anything after a '?' or '#'.
    auto cut = id.find_first_of("?#");
    if (cut != std::string::npos) id = id.substr(0, cut);
    while (!id.empty() && id.back() == '/') id.pop_back();
    return id;
}

@interface HanabiURLHandler : NSObject
- (void)handleGetURLEvent:(NSAppleEventDescriptor*)event
           withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation HanabiURLHandler
- (void)handleGetURLEvent:(NSAppleEventDescriptor*)event
           withReplyEvent:(NSAppleEventDescriptor*)reply {
    (void)reply;
    NSString* urlStr =
        [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    std::string id = parse_thread_url(urlStr);
    if (id.empty()) return;
    set_pending_open_thread(id);
    NSLog(@"native_extras: hanabi://thread open -> id=%s", id.c_str());
    // Bring hanabi forward so the opened thread is visible.
    [NSApp activateIgnoringOtherApps:YES];
}
@end

static HanabiURLHandler* g_url_handler = nil;

void native_openurl_install(void) {
    if (g_url_handler != nil) return;   // already installed
    if (NSApp == nil) return;           // needs the app object
    g_url_handler = [[HanabiURLHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:g_url_handler
            andSelector:@selector(handleGetURLEvent:withReplyEvent:)
          forEventClass:kInternetEventClass
             andEventID:kAEGetURL];
    NSLog(@"native_extras: hanabi:// URL handler installed");
}

bool native_take_open_thread(char* out, int cap) {
    if (out == nullptr || cap <= 0) return false;
    std::string id;
    {
        std::lock_guard<std::mutex> lk(g_open_thread_mu);
        if (g_pending_open_thread.empty()) return false;
        id.swap(g_pending_open_thread);
    }
    std::strncpy(out, id.c_str(), static_cast<size_t>(cap - 1));
    out[cap - 1] = '\0';
    return true;
}

// ====================================================================
// 6. Image attachments — clipboard paste + file drop
// ====================================================================
//
// Both halves answer with a PATH (see the contract in native_extras.h). The
// C++ core never sees an NSImage, an NSPasteboard or an NSDraggingInfo.
//
// WHY A CATEGORY ON MTKView, and not a window delegate or an overlay view.
// sokol_app owns the window and its content view: it builds the sapp_desc
// itself inside afterhours (backends/sokol/backend.h), which is vendored and
// read-only, and it never sets desc.enable_dragndrop — so sokol's own
// drag-and-drop is unreachable from here (afterhours_gaps.md #60). Of the ways
// to get a drop into a window we do not own:
//   * replacing the window's delegate would take sokol's resize/close
//     callbacks away from it;
//   * an overlay NSView registered for dragged types has to be hit-testable to
//     be found as a drag destination, and a hit-testable view over the content
//     view eats every mouse event the UI needs;
//   * a category adding (never overriding) the NSDraggingDestination methods
//     to the content view's class costs nothing else: MTKView does not
//     implement them, this process has exactly one MTKView — sokol's — and the
//     registration below is what actually turns the behaviour on.
// The guard in native_filedrop_install() reports honestly if the content view
// is ever not an MTKView, rather than silently accepting no drops.

#import <MetalKit/MetalKit.h>

// The image types a chip can show and a future send path could carry. Kept in
// step with what the orchestrator's message route accepts today (png, jpeg,
// gif, webp) so hanabi never takes in a file the backend would refuse.
static bool hanabi_is_image_path(NSString* path) {
    if (path == nil) return false;
    NSString* ext = [[path pathExtension] lowercaseString];
    return [ext isEqualToString:@"png"] || [ext isEqualToString:@"jpg"] ||
           [ext isEqualToString:@"jpeg"] || [ext isEqualToString:@"gif"] ||
           [ext isEqualToString:@"webp"];
}

// ---- the pending-drop queue (AppKit writes, the frame loop drains) ---------
static std::mutex g_drop_mu;
static std::vector<std::string> g_pending_drops;  // guarded by g_drop_mu

static void push_dropped_path(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lk(g_drop_mu);
    // A drop the frame loop has not drained yet is not thrown away, but a user
    // who drags a folder of a hundred images should not be able to make this
    // grow without bound: the composer caps what it will hold anyway.
    if (g_pending_drops.size() >= 16) return;
    g_pending_drops.push_back(path);
}

@interface MTKView (HanabiFileDrop) <NSDraggingDestination>
@end

@implementation MTKView (HanabiFileDrop)

// Only say yes to a drag we would actually take, so the cursor shows the copy
// badge over an image and the no-entry sign over anything else.
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL* u in urls)
        if (hanabi_is_image_path([u path])) return NSDragOperationCopy;
    return NSDragOperationNone;
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return YES;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    int taken = 0;
    for (NSURL* u in urls) {
        if (!hanabi_is_image_path([u path])) continue;
        const char* p = [[u path] UTF8String];
        if (p == nullptr) continue;
        push_dropped_path(std::string(p));
        ++taken;
    }
    if (taken > 0) NSLog(@"native_extras: file drop -> %d image(s)", taken);
    return taken > 0;
}

@end

void native_filedrop_install(void) {
    static bool installed = false;
    if (installed) return;
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (window == nil) window = [NSApp keyWindow];
        if (window == nil)
            for (NSWindow* w in [NSApp windows])
                if ([w isVisible]) { window = w; break; }
        if (window == nil) {
            HLOG(@"native_extras: filedrop install deferred (no window yet)");
            return;  // not installed — the caller retries next frame
        }
        NSView* view = [window contentView];
        if (![view isKindOfClass:[MTKView class]]) {
            NSLog(@"native_extras: filedrop NOT installed — content view is %@,"
                  @" not MTKView; the drag methods live on MTKView",
                  [view class]);
            installed = true;  // retrying cannot help
            return;
        }
        [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        installed = true;
        NSLog(@"native_extras: file-drop destination installed (image files)");
    }
}

bool native_take_dropped_image(char* out, int cap) {
    if (out == nullptr || cap <= 0) return false;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_drop_mu);
        if (g_pending_drops.empty()) return false;
        path = g_pending_drops.front();
        g_pending_drops.erase(g_pending_drops.begin());
    }
    std::strncpy(out, path.c_str(), static_cast<size_t>(cap - 1));
    out[cap - 1] = '\0';
    return true;
}

void native_simulate_file_drop(const char* path) {
    if (path == nullptr || path[0] == '\0') return;
    push_dropped_path(std::string(path));
}

// ---- clipboard ------------------------------------------------------------

// Where a paste is read from. The general pasteboard in every real run; a
// NAMED one when HANABI_PASTEBOARD_NAME is set, which is how this path gets
// exercised by hand without writing over whatever the user has copied (the
// scripted harness cannot press a Cmd chord — afterhours_gaps.md #49 — so a
// paste test is a manual one, and a manual test must not cost the user their
// clipboard).
static NSPasteboard* hanabi_paste_source(void) {
    const char* name = getenv("HANABI_PASTEBOARD_NAME");
    if (name != nullptr && name[0] != '\0')
        return [NSPasteboard pasteboardWithName:[NSString
                                                    stringWithUTF8String:name]];
    return [NSPasteboard generalPasteboard];
}

// Raw pasted pixels have no file behind them, so give them one: a PNG under
// the temp dir, named uniquely so two pastes never collide. Returns "" if the
// data cannot be turned into a PNG or cannot be written.
static std::string write_temp_png(NSData* png) {
    if (png == nil || [png length] == 0) return std::string();
    NSString* dir = NSTemporaryDirectory();
    NSString* name =
        [NSString stringWithFormat:@"hanabi-paste-%@.png", [[NSUUID UUID] UUIDString]];
    NSString* path = [dir stringByAppendingPathComponent:name];
    NSError* err = nil;
    if (![png writeToFile:path options:NSDataWritingAtomic error:&err]) {
        NSLog(@"native_extras: could not write pasted image: %@", err);
        return std::string();
    }
    const char* p = [path UTF8String];
    return p != nullptr ? std::string(p) : std::string();
}

bool native_take_clipboard_image(char* out, int cap) {
    if (out == nullptr || cap <= 0) return false;
    @autoreleasepool {
        NSPasteboard* pb = hanabi_paste_source();
        if (pb == nil) return false;

        // 1. An image FILE copied in Finder. It already lives somewhere; a
        // temp copy would only be a second name for the same pixels.
        NSArray<NSURL*>* urls = [pb
            readObjectsForClasses:@[ [NSURL class] ]
                          options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
        for (NSURL* u in urls) {
            if (!hanabi_is_image_path([u path])) continue;
            const char* p = [[u path] UTF8String];
            if (p == nullptr) continue;
            std::strncpy(out, p, static_cast<size_t>(cap - 1));
            out[cap - 1] = '\0';
            return true;
        }

        // 2. Raw image data — a screenshot (Cmd+Ctrl+Shift+4), an image copied
        // from a browser. PNG if the pasteboard has it; otherwise anything
        // NSImage can read is re-encoded, because the chip and every future
        // send path want ONE format rather than whatever the source felt like.
        NSData* png = [pb dataForType:NSPasteboardTypePNG];
        if (png == nil) {
            NSImage* img = [[NSImage alloc] initWithPasteboard:pb];
            if (img == nil) return false;
            NSData* tiff = [img TIFFRepresentation];
            if (tiff == nil) return false;
            NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:tiff];
            if (rep == nil) return false;
            png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}];
        }
        const std::string path = write_temp_png(png);
        if (path.empty()) return false;
        std::strncpy(out, path.c_str(), static_cast<size_t>(cap - 1));
        out[cap - 1] = '\0';
        NSLog(@"native_extras: pasted image -> %s", path.c_str());
        return true;
    }
}

// ===========================================================================
// 7. Native file picker (NSOpenPanel)
// ===========================================================================
//
// Unlike everything above it, this one is not a latch the frame loop drains:
// a picker is a QUESTION, asked at the moment of a click and answered before
// the click is finished. runModal blocks the caller — the UI freezes behind
// the panel, which is exactly what a modal sheet looks like everywhere else on
// this OS — and the answer is returned, so no app state is touched from here.
// The single-owner rule is kept: the caller writes what it learns.

bool native_pick_directory(const char* prompt, char* out, int cap) {
    if (out == nullptr || cap <= 0) return false;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.allowsMultipleSelection = NO;
        // A destination the user is allowed to invent: exporting into a folder
        // that does not exist yet is a reasonable thing to want.
        panel.canCreateDirectories = YES;
        if (prompt != nullptr && prompt[0] != '\0')
            panel.prompt = [NSString stringWithUTF8String:prompt];

        // A bare executable is not a foreground app until it says so; without
        // this the panel can open behind the window it belongs to.
        [NSApp activateIgnoringOtherApps:YES];

        if ([panel runModal] != NSModalResponseOK) return false;
        NSURL* url = [[panel URLs] firstObject];
        if (url == nil) return false;
        const char* path = [[url path] UTF8String];
        if (path == nullptr || path[0] == '\0') return false;
        std::strncpy(out, path, static_cast<size_t>(cap - 1));
        out[cap - 1] = '\0';
        NSLog(@"native_extras: export destination chosen -> %s", out);
        return true;
    }
}

// ===========================================================================
// 8. Opening a link
// ===========================================================================
// Open a URL in whatever the user has set as their browser. Only http/https
// are passed on: a message can say anything, and "file:///" or a custom
// scheme reaching NSWorkspace from message text is a way to make the app do
// something on behalf of whoever wrote the message.
void native_open_url(const char* url) {
    if (url == nullptr || *url == '\0') return;
    NSString* s = [NSString stringWithUTF8String:url];
    if (s == nil) return;
    if (![s hasPrefix:@"http://"] && ![s hasPrefix:@"https://"]) {
        NSLog(@"native_extras: refusing to open non-http URL");
        return;
    }
    NSURL* u = [NSURL URLWithString:s];
    if (u == nil) return;
    [[NSWorkspace sharedWorkspace] openURL:u];
}
