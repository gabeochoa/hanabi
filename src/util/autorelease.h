#pragma once

// ---------------------------------------------------------------------------
// An autorelease pool around one frame.
//
// THE BUG THIS FIXES. Metal hands back autoreleased Objective-C objects:
// `sg_begin_pass` alone produces a command buffer, a render-pass descriptor
// and its colour, depth and stencil attachment descriptors — six objects a
// frame, none of them owned by the caller. In a normal Cocoa app the run
// loop's own pool drains every iteration and nobody thinks about it. A render
// loop that calls into Metal WITHOUT a pool has nothing draining it, so every
// frame's six objects stay live for the life of the process.
//
// Measured, before the fix, over a 10,248-frame idle run:
//
//   10248 calls  9182208 bytes  -[AGXG16XFamilyCommandQueue commandBufferWithUnretainedReferences]
//   10248 calls  6558720 bytes  -[AGXG16XFamilyCommandBuffer initWithQueue:retainedReferences:]
//   10248 calls  3279360 bytes  +[MTLRenderPassDescriptor renderPassDescriptor]
//   10248 calls  2295552 bytes  +[MTLRenderPassColorAttachmentDescriptor allocWithZone:]
//   10248 calls  2295552 bytes  +[MTLRenderPassDepthAttachmentDescriptor allocWithZone:]
//   10248 calls  2295552 bytes  +[MTLRenderPassStencilAttachmentDescriptor allocWithZone:]
//
// Exactly one of each per frame, ~2.5 KB a frame, ~9 MB a minute at 60fps, and
// it never comes back. That is the whole of the reported "it gets slower and
// slower every second until it freezes": the process grows without bound, and
// the allocator and the VM system get slower as it does.
//
// WHY IT IS HERE AND NOT IN THE LIBRARY. `sg_begin_pass` is reached through
// afterhours' sokol backend, and `vendor/afterhours` is read-only — twenty
// projects share it. The pool belongs to whoever owns the loop anyway: a
// library cannot know where a caller's frame begins and ends, and draining at
// the wrong point frees something still in use. hanabi owns its loop, so
// hanabi drains it.
//
// WHY THE C API rather than `@autoreleasepool`. That keyword needs the file to
// be Objective-C++, and main.cpp is not — making it `.mm` to get a pool would
// recompile the whole app under a different front end for two function calls.
// These two are the exact functions the compiler emits for the keyword; they
// are in libobjc, which is already linked on every Apple platform.
// ---------------------------------------------------------------------------

namespace hanabi {

#if defined(__APPLE__)

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);

// Scoped, so an early return or a throw inside the frame still drains. A frame
// that returns early is exactly when a leak is least likely to be noticed.
class AutoreleaseFrame {
public:
    AutoreleaseFrame() : pool_(objc_autoreleasePoolPush()) {}
    ~AutoreleaseFrame() { objc_autoreleasePoolPop(pool_); }

    AutoreleaseFrame(const AutoreleaseFrame&) = delete;
    AutoreleaseFrame& operator=(const AutoreleaseFrame&) = delete;

private:
    void* pool_;
};

#else

// Non-Apple: nothing to drain, and the type still exists so call sites need no
// conditional compilation of their own.
class AutoreleaseFrame {
public:
    AutoreleaseFrame() = default;
    AutoreleaseFrame(const AutoreleaseFrame&) = delete;
    AutoreleaseFrame& operator=(const AutoreleaseFrame&) = delete;
};

#endif

}  // namespace hanabi
