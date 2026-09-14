#pragma once
// The capture receipt: where a painted UI rectangle IS, in every space an
// outside `screencapture -l <window>` needs to crop it -- read from AppKit,
// never guessed.
//
// A crop of an owned window's capture has been guessed at repeatedly: a
// titlebar of 0 or 56, a shadow margin, an alignment to the reference's own
// hairlines. Each guess moved rows. This is the one place the mapping is
// computed, from the same objects the window server uses:
//
//   content space (afterhours logical px, top-left origin)
//     -> window_manager::content_to_window   (the letterbox's inverse; the
//        pointer's window_to_content is the same Viewport the other way)
//     -> AppKit contentView frame flip       (bottom-left origin, points)
//     -> [NSWindow convertRectToScreen:]     (screen points, bottom-left)
//     -> screen top-left points              (main screen height flip)
//     -> x backingScaleFactor                (CGImage pixels)
//
// and the rect's position INSIDE a capture of THIS window alone
// (`screencapture -x -o -l <windowNumber>` captures the window's frame,
// without its shadow when -o is given) is `(rect_screen - frame_screen) x
// scale`. That is the documented rigid transform: crop the capture at
// `crop_x, crop_y, crop_w, crop_h` pixels. Nothing else is applied.
//
// Everything is a read. The probe changes no window, view, focus, or
// preference, and posts no event.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HanabiNativeWindowReceipt {
    int ok;                 // 0 = no usable window; `why` says
    char why[160];
    long window_number;     // [NSWindow windowNumber]
    int pid;                // getpid(): the capture's owner
    int owned;              // 1 = the window's owning PID is this process
    int visible;            // [NSWindow isVisible]
    int on_active_space;    // [NSWindow isOnActiveSpace]
    double backing_scale;   // [NSWindow backingScaleFactor]
    // Screen-space rectangles in POINTS, TOP-LEFT origin (y grows down), the
    // way an image reader thinks; AppKit's bottom-left values are converted
    // with the window's own screen height.
    double frame_x, frame_y, frame_w, frame_h;        // [NSWindow frame]
    double content_x, content_y, content_w, content_h; // contentView, in screen
    // AppKit's own words for the same, bottom-left origin, for cross-checking.
    double frame_bl_x, frame_bl_y;
    double content_bl_x, content_bl_y;
    double screen_h;        // the window's screen frame height, points
} HanabiNativeWindowReceipt;

// Read the app's main/key/visible window. `out` is fully written.
void hanabi_native_window_receipt(HanabiNativeWindowReceipt* out);

// Convert a rect in the CONTENT VIEW's coordinate space (points, top-left
// origin, as the app lays out) to screen points, top-left origin, through
// [NSWindow convertRectToScreen:] -- no arithmetic on titlebar heights.
// Returns 0 when there is no window. Also returns the rect's offset inside
// the window's frame (points), which is what a capture of this window alone
// is cropped by.
int hanabi_native_content_rect_to_screen(double cx, double cy, double cw, double ch,
                                         double* sx, double* sy, double* sw, double* sh,
                                         double* in_frame_x, double* in_frame_y);

#ifdef __cplusplus
}
#endif
