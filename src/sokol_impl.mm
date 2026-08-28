// sokol_impl.mm
// Objective-C++ file that compiles the Sokol implementations for Metal on macOS.
// This must be compiled as a single translation unit with SOKOL_IMPL defined.

#define SOKOL_IMPL
#define SOKOL_METAL
#define SOKOL_NO_ENTRY

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <sokol/sokol_log.h>

// 2D drawing and text rendering
#define SOKOL_GL_IMPL
#include <sokol/sokol_gl.h>

#define FONTSTASH_IMPLEMENTATION
#include <fontstash/stb_truetype.h>
#include <fontstash/fontstash.h>

#define SOKOL_FONTSTASH_IMPL
#include <sokol/sokol_fontstash.h>

// Image decoding (stb_image) rides along with this SOKOL_IMPL TU, and this also
// defines the afterhours sokol-impl sentinel that graphics::run() references.
#include <afterhours/src/backends/sokol/image_decode.h>

// Metal device creation + frame capture (extern "C" symbols called from the
// header-only backend). Must be included from the SOKOL_IMPL .mm after the
// sokol headers.
#include <afterhours/src/backends/sokol/capture_impl.h>
#include <atomic>

#import <AppKit/AppKit.h>

static std::atomic<bool> g_hanabi_window_resize{true};
static std::atomic<bool> g_hanabi_window_exposure{true};
static id g_hanabi_window_activity_observer = nil;

@interface HanabiWindowActivityObserver : NSObject
@end

@implementation HanabiWindowActivityObserver
- (void)resized:(NSNotification*)note {
    (void)note;
    g_hanabi_window_resize.store(true);
}
- (void)exposed:(NSNotification*)note {
    (void)note;
    g_hanabi_window_exposure.store(true);
}
@end

extern "C" void metal_frame_activity_install(void) {
    if (g_hanabi_window_activity_observer != nil) return;
    HanabiWindowActivityObserver* observer =
        [[HanabiWindowActivityObserver alloc] init];
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver:observer selector:@selector(resized:)
                   name:NSWindowDidResizeNotification object:nil];
    for (NSNotificationName name in @[
             NSWindowDidExposeNotification,
             NSWindowDidBecomeKeyNotification,
             NSWindowDidMiniaturizeNotification,
             NSWindowDidDeminiaturizeNotification,
             NSWindowDidChangeBackingPropertiesNotification,
             NSApplicationDidBecomeActiveNotification]) {
        [center addObserver:observer selector:@selector(exposed:)
                       name:name object:nil];
    }
    g_hanabi_window_activity_observer = observer;
}

extern "C" unsigned metal_take_window_activity(void) {
    unsigned activity = 0;
    if (g_hanabi_window_resize.exchange(false)) activity |= 1u;
    if (g_hanabi_window_exposure.exchange(false)) activity |= 2u;
    return activity;
}


extern "C" void metal_set_window_size(int width, int height) {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            window = [NSApp keyWindow];
        }
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) {
                    window = w;
                    break;
                }
            }
        }
        if (!window) {
            NSLog(@"metal_set_window_size: no window available");
            return;
        }

        // Get the current frame and compute the new one.
        // Keep the top-left corner anchored (macOS uses bottom-left origin).
        NSRect frame = [window frame];
        CGFloat titleBarHeight = frame.size.height - [[window contentView] frame].size.height;
        CGFloat newHeight = (CGFloat)height + titleBarHeight;
        CGFloat newWidth = (CGFloat)width;

        // Dark window background so any area exposed during a resize doesn't
        // flash white before the app redraws it.
        window.backgroundColor = [NSColor colorWithSRGBRed:0.1176
                                                     green:0.1176
                                                      blue:0.1176
                                                     alpha:1.0];

        // CRITICAL: pin the Metal view's layer to the top-left corner (no scale)
        // for this resize. Sokol renders into an MTKView whose layerContentsPlacement
        // defaults to a *scaling* value, so on a programmatic setFrame AppKit
        // stretches the last-drawn frame (sidebar included) to fill the new bounds
        // until the next frame is drawn — that's the sidebar "moving/resizing".
        // Anchoring top-left keeps the old frame 1:1 where the sidebar lives; the
        // newly exposed strip just shows the dark bg until the next redraw.
        // (Same workaround sokol applies for user drags; see sokol #700/#727.)
        NSView* contentView = [window contentView];
        if (contentView.layer) {
            contentView.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;
        }

        // Anchor top-left: adjust origin.y so the top edge stays put.
        // Instant (animate:NO): the animated variant desyncs from the Metal
        // drawable, causing a white flash on grow and content-snap on shrink.
        CGFloat deltaH = newHeight - frame.size.height;
        NSRect newFrame = NSMakeRect(frame.origin.x, frame.origin.y - deltaH,
                                     newWidth, newHeight);
        [window setFrame:newFrame display:YES animate:NO];
    }
}

// Constrain the window to the screen's VISIBLE frame (below the menu bar, above
// the Dock) so its bottom edge — where the composer + status bar live — is
// always on-screen. A user can drag/resize the window taller than the display
// (macOS also restores a too-large frame across launches), which pushes the
// composer BELOW the visible area — it renders, but you can't see or reach it
// ("HOW DO I SEND A MESSAGE" — the input was off the bottom of the screen).
// Idempotent + cheap; safe to call on the first frame and on activate.
extern "C" void metal_constrain_window_to_screen(void) {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) window = [NSApp keyWindow];
        if (!window) {
            for (NSWindow* w in [NSApp windows]) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (!window) return;
        NSScreen* screen = [window screen];
        if (!screen) screen = [NSScreen mainScreen];
        if (!screen) return;
        NSRect vis = [screen visibleFrame];   // excludes menu bar + Dock
        NSRect frame = [window frame];
        NSRect nf = frame;
        // Clamp size to the visible area (leave a small margin so the frame
        // isn't flush to the screen edges).
        CGFloat maxW = vis.size.width;
        CGFloat maxH = vis.size.height;
        if (nf.size.width > maxW) nf.size.width = maxW;
        if (nf.size.height > maxH) nf.size.height = maxH;
        // Reposition so the whole frame is inside vis (fix a bottom/edge that
        // spilled off-screen). macOS origin is bottom-left.
        if (nf.origin.x < vis.origin.x) nf.origin.x = vis.origin.x;
        if (nf.origin.y < vis.origin.y) nf.origin.y = vis.origin.y;
        if (nf.origin.x + nf.size.width > vis.origin.x + vis.size.width)
            nf.origin.x = vis.origin.x + vis.size.width - nf.size.width;
        if (nf.origin.y + nf.size.height > vis.origin.y + vis.size.height)
            nf.origin.y = vis.origin.y + vis.size.height - nf.size.height;
        if (!NSEqualRects(nf, frame))
            [window setFrame:nf display:YES animate:NO];
    }
}

#import <objc/runtime.h>

static id _e2e_activity_token = nil;

extern "C" void metal_activate_app(void) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (window) {
            [window makeKeyAndOrderFront:nil];
        }

        if (!_e2e_activity_token) {
            _e2e_activity_token = [[NSProcessInfo processInfo]
                beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep |
                                         NSActivityLatencyCritical)
                reason:@"E2E Testing"];
        }
    }
}

#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

extern "C" void metal_take_screenshot(const char* filename) {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            window = [NSApp keyWindow];
        }
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) {
                    window = w;
                    break;
                }
            }
        }
        if (!window) {
            NSLog(@"take_screenshot: no window available");
            return;
        }

        CGWindowID windowID = (CGWindowID)[window windowNumber];
        char wid_str[32];
        snprintf(wid_str, sizeof(wid_str), "%u", windowID);

        char* argv[] = {
            (char*)"/usr/sbin/screencapture",
            (char*)"-x", (char*)"-o",
            (char*)"-l", wid_str,
            (char*)filename,
            nullptr
        };

        pid_t pid;
        int ret = posix_spawn(&pid, "/usr/sbin/screencapture",
                              nullptr, nullptr, argv, environ);
        if (ret == 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            NSLog(@"take_screenshot: posix_spawn failed with %d", ret);
        }
    }
}

extern "C" void metal_wait_all_screenshots(void) {
    // no-op: screenshots are taken synchronously
}

static bool _headless_mode = false;

extern "C" void metal_hide_window(void) {
    @autoreleasepool {
        _headless_mode = true;

        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (window) {
            // Move off-screen rather than orderOut: so the Metal display
            // link keeps firing at full speed and screencapture -l still
            // works (captures the window's backing store by window ID).
            NSRect frame = [window frame];
            [window setFrame:NSMakeRect(-20000, -20000, frame.size.width, frame.size.height)
                     display:YES animate:NO];
        }

        // Suppress idle sleep throttling even though the window is off-screen
        if (!_e2e_activity_token) {
            _e2e_activity_token = [[NSProcessInfo processInfo]
                beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep |
                                         NSActivityLatencyCritical)
                reason:@"E2E Headless Testing"];
        }
    }
}

extern "C" bool metal_is_headless(void) {
    return _headless_mode;
}

// Read the macOS "natural scrolling" system preference. This is the global
// default `com.apple.swipescrolldirection` in NSGlobalDomain: true (1) means
// natural scrolling is ON (content follows the fingers), false/absent means
// it's OFF (classic/inverted). This is the value flipped by
// System Settings -> Trackpad/Mouse -> "Natural scrolling".
//
// We read it at startup to decide whether the UI's scroll wheel handling
// should invert its offset sign so the app tracks the OS setting instead of
// hard-coding one direction. Reading the persisted default (rather than
// NSEvent.isDirectionInvertedFromDevice, which needs a live event) is correct
// at launch and needs no event pump.
extern "C" bool macos_natural_scroll(void) {
    @autoreleasepool {
        return [[NSUserDefaults standardUserDefaults]
                   boolForKey:@"com.apple.swipescrolldirection"];
    }
}

// True when the OS is in Dark appearance. Lets the "System" theme choice track
// the real macOS setting instead of falling back to Dark (was gap #16). Uses
// AppleInterfaceStyle == "Dark" (the standard, dependency-free probe); absent
// key = Light. Read on demand (theme apply), cheap.
extern "C" bool macos_is_dark_mode(void) {
    @autoreleasepool {
        NSString* style = [[NSUserDefaults standardUserDefaults]
                              stringForKey:@"AppleInterfaceStyle"];
        return style != nil &&
               [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
    }
}
