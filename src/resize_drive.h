#pragma once

// A LIVE window resize, driven from inside the process.
//
// A person resizing the window puts AppKit into its resize tracking loop:
// NSThemeFrame takes the mouse-down at the frame's edge, then pulls
// mouse-dragged events off the application's queue in
// NSEventTrackingRunLoopMode, moving the frame for each one --
// windowWillStartLiveResize, then windowDidResize per step, then
// windowDidEndLiveResize -- while MTKView's display link keeps drawing frames.
// That is the path this app has to be fast on, and no headless scenario
// reaches it: set_window_size is a programmatic setFrame that never enters
// the tracking loop.
//
// osascript / CGEvent could drag the corner from outside, but both need the
// Accessibility grant, which is not a permission to hand a build machine to
// settle a measurement. So the events are made here with +[NSEvent
// mouseEventWithType:...] and posted to the queue AppKit reads
// (-[NSApplication postEvent:atStart:]). AppKit cannot tell them from the
// window server's: the same theme frame, the same tracking loop, the same
// notifications. What this does NOT exercise is the window server's own
// side (the Metal layer's presentation timing against a real cursor), and
// the report says so.
//
// HANABI_RESIZE_DRIVE=<pattern>[,<pattern>...] arms it on the first windowed
// frame; the process quits when the pattern is done. Patterns:
//   grow:<dw>x<dh>:<steps>      drag the bottom-right corner by (dw,dh) over N
//                               mouse-dragged events (negative shrinks)
//   hold:<n>                    n ticks with no event (the settle)
//   e.g. HANABI_RESIZE_DRIVE=grow:400x200:60,grow:-600x-300:60,grow:300x150:30,hold:60
// One tick per HANABI_RESIZE_DRIVE_TICK_MS (default 8, i.e. a 120 Hz mouse).
//
// Output (stdout), one line per frame the app rendered while armed:
//   [resize-drive] frame <n> t=<ms since arm> win=<w>x<h> fb=<w>x<h> cpu=<ms> wall=<ms> live=<0|1> notes=<k>
// and one summary line at the end, in the shape scripts/resize_drive_gate.sh
// reads. `notes` is the number of windowDidResize notifications since the
// previous frame -- a frame that painted while several sizes went by is a
// frame the person saw lag behind their pointer.

namespace hanabi::resize_drive {

// True when HANABI_RESIZE_DRIVE is set (read once).
bool armed();

// Called from the windowed frame loop once the window exists; installs the
// observers and the tick timer on the first call, no-op after.
void install();

// Called at the top of every frame CALLBACK the backend makes (before the
// frame policy decides whether to render), so callbacks and rendered frames
// can be counted apart: a drag whose callbacks stop is a different defect
// from one whose frames are skipped.
void callback();

// Called by the windowed frame loop around the app's frame, so the driver
// can attribute CPU and wall time to the size the frame was drawn at.
void frame_begin();
void frame_end();

// True once the pattern has run out and the summary is printed; the frame
// loop quits on it.
bool finished();

}  // namespace hanabi::resize_drive

// ---------------------------------------------------------------------------
// The NATIVE input bridge for scripts (tests/ui/*.e2e), test-only.
//
// The e2e harness's `click`/`type` inject straight into afterhours' input
// tables, BEFORE the backend and before the letterbox that maps a window
// point onto content (input_system.h letterboxed_mouse_position). A script
// driven that way cannot see a defect in that mapping. These post real
// NSEvents through -[NSApplication postEvent:atStart:], the path a mouse's
// events take from the window server's queue onward: NSEvent -> sokol's view
// -> sapp event -> afterhours backend state -> window_to_content -> hit-test.
// Content-space coordinates in: logical points, origin at the TOP-LEFT of the
// window's contentView -- the same space the app's painted rects (assert_ui)
// live in -- converted to window base coordinates with the CURRENT frame.
// No correction of any kind is applied: if the mapping downstream is wrong,
// the click lands wrong, which is the point.
//
// Not the window server: presentation timing and the server's own delivery
// are not exercised (afterhours_gaps.md #594 has the limit).
extern "C" {
void hanabi_native_mouse_move(float content_x, float content_y);
void hanabi_native_mouse_down(float content_x, float content_y, int right);
void hanabi_native_mouse_up(float content_x, float content_y, int right);
// A drag of the bottom-right resize corner by (dw, dh) in `steps` mouse-dragged
// events, posted at once; AppKit's tracking loop consumes them and the window
// ends resized (a live resize, WillStart/DidEnd fire). Content size afterwards
// is read back by hanabi_native_content_size.
void hanabi_native_drag_resize(int dw, int dh, int steps);
void hanabi_native_content_size(float* w, float* h);
// A key press (down + up) for one character with modifier flags
// (1 = shift, 2 = control, 4 = option, 8 = command); `key_code` is the
// macOS virtual key code, `chars` the characters the key produces (may be
// empty for a pure modifier or arrow). Goes through the view's keyDown, so the
// responder chain and sokol's key/char events are the real ones.
void hanabi_native_key(unsigned short key_code, const char* chars, unsigned mods);
// Bring the app forward and make its window key. A key equivalent (Cmd+W,
// Cmd+1) is dispatched by NSApplication to the MAIN MENU, and a menu only
// answers for the frontmost app -- so a script that presses a chord has to
// activate first, exactly as a person clicking the window would.
void hanabi_native_activate(void);
// Whether there is a window at all: a script that drives native input in a
// headless run has nothing to drive, and must fail rather than pass quietly.
int hanabi_native_has_window(void);
// The modifier part of a chord, held and released on separate FRAMES: a
// polled modifier read (keys.h cmd_down) only sees a modifier that is still
// down when the frame runs, so posting press and release in one batch leaves
// every poll reading false.
void hanabi_native_mods_down(unsigned mods);
void hanabi_native_mods_up(unsigned mods);
}
