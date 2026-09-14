// See native_capture_probe.h. AppKit reads only; nothing here posts an event,
// orders a window, or touches a preference.
#include "native_capture_probe.h"

#import <AppKit/AppKit.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {
NSWindow* the_window() {
    NSWindow* w = [NSApp mainWindow];
    if (w == nil) w = [NSApp keyWindow];
    if (w == nil)
        for (NSWindow* c in [NSApp windows])
            if ([c isVisible]) return c;
    return w;
}

// The window's own screen: its height is what flips AppKit's bottom-left
// points to an image reader's top-left. The window's screen, not the main
// one: a window on a second display must be flipped by that display.
double screen_height_for(NSWindow*) {
    // Every AppKit screen rect is in ONE global bottom-left space whose origin
    // is the primary screen's bottom-left, so the flip line for a top-left y
    // is the primary screen's maxY -- for a window on any display. (A
    // per-display height would be wrong for a secondary screen.)
    NSScreen* primary = [[NSScreen screens] firstObject];
    return NSMaxY([primary frame]);
}

// Whether the window number belongs to THIS process, asked of the window
// server rather than assumed from NSApp: the capture is keyed by the number,
// and a stale or foreign number would crop someone else's window.
int window_owned_by_this_pid(long windowNumber) {
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                 static_cast<CGWindowID>(windowNumber));
    if (list == nullptr) return 0;
    int owned = 0;
    if (CFArrayGetCount(list) == 1) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(list, 0);
        NSNumber* pid = info[(__bridge NSString*)kCGWindowOwnerPID];
        owned = (pid != nil && [pid intValue] == getpid()) ? 1 : 0;
    }
    CFRelease(list);
    return owned;
}
}  // namespace

extern "C" void hanabi_native_window_receipt(HanabiNativeWindowReceipt* out) {
    if (out == nullptr) return;
    std::memset(out, 0, sizeof *out);
    @autoreleasepool {
        NSWindow* win = the_window();
        if (win == nil) {
            std::snprintf(out->why, sizeof out->why, "no window (headless, or none visible)");
            return;
        }
        out->pid = static_cast<int>(getpid());
        out->window_number = static_cast<long>([win windowNumber]);
        out->owned = window_owned_by_this_pid(out->window_number);
        out->visible = [win isVisible] ? 1 : 0;
        out->on_active_space = [win isOnActiveSpace] ? 1 : 0;
        out->backing_scale = [win backingScaleFactor];
        const double H = screen_height_for(win);
        out->screen_h = H;

        const NSRect frame = [win frame];
        out->frame_bl_x = frame.origin.x;
        out->frame_bl_y = frame.origin.y;
        out->frame_x = frame.origin.x;
        out->frame_y = H - NSMaxY(frame);
        out->frame_w = frame.size.width;
        out->frame_h = frame.size.height;

        NSView* cv = [win contentView];
        const NSRect cvScreen = [win convertRectToScreen:[cv convertRect:[cv bounds] toView:nil]];
        out->content_bl_x = cvScreen.origin.x;
        out->content_bl_y = cvScreen.origin.y;
        out->content_x = cvScreen.origin.x;
        out->content_y = H - NSMaxY(cvScreen);
        out->content_w = cvScreen.size.width;
        out->content_h = cvScreen.size.height;

        if (!out->owned) {
            std::snprintf(out->why, sizeof out->why,
                          "window %ld is not owned by pid %d per the window server",
                          out->window_number, out->pid);
            return;
        }
        if (!out->visible) {
            std::snprintf(out->why, sizeof out->why, "window %ld is not visible",
                          out->window_number);
            return;
        }
        out->ok = 1;
    }
}

extern "C" int hanabi_native_content_rect_to_screen(double cx, double cy, double cw, double ch,
                                                    double* sx, double* sy, double* sw,
                                                    double* sh, double* in_frame_x,
                                                    double* in_frame_y) {
    @autoreleasepool {
        NSWindow* win = the_window();
        if (win == nil) return 0;
        NSView* cv = [win contentView];
        // Content space is top-left; the view's coordinate space is
        // bottom-left unless flipped -- convert by the view's own bounds, not
        // by a remembered height.
        const NSRect b = [cv bounds];
        NSRect inView = NSMakeRect(b.origin.x + cx, b.origin.y + b.size.height - (cy + ch), cw, ch);
        if ([cv isFlipped]) inView = NSMakeRect(b.origin.x + cx, b.origin.y + cy, cw, ch);
        const NSRect inWindow = [cv convertRect:inView toView:nil];
        const NSRect onScreen = [win convertRectToScreen:inWindow];
        const double H = screen_height_for(win);
        if (sx) *sx = onScreen.origin.x;
        if (sy) *sy = H - NSMaxY(onScreen);
        if (sw) *sw = onScreen.size.width;
        if (sh) *sh = onScreen.size.height;
        const NSRect frame = [win frame];
        if (in_frame_x) *in_frame_x = onScreen.origin.x - frame.origin.x;
        if (in_frame_y) *in_frame_y = NSMaxY(frame) - NSMaxY(onScreen);
        return 1;
    }
}
