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
