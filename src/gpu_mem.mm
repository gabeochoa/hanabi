// gpu_mem.mm
// The one place an Objective-C type is needed to answer "how many GPU bytes
// does this process hold?". Declared in src/util/gpu_mem.h; see the comment
// there for why the app needs a number the malloc zones cannot give it.
//
// -[MTLDevice currentAllocatedSize] is the driver's own total for every
// resource allocated on this device: textures, buffers, heaps, pipeline state.
// It is the ground truth the estimate in gpu_mem.h is checked against, and it
// is the only counter here that sees the font atlas, the glyph textures and
// the offscreen render target -- all of which are created inside afterhours,
// which reports nothing (afterhours_gaps.md #126).
//
// sokol hands out the device it created rather than us making a second one:
// -[MTLCreateSystemDefaultDevice] would return a DIFFERENT device object whose
// currentAllocatedSize is zero, which is the shape of a fake green -- a number
// that is fast, plausible, and measuring nothing.

#import <Metal/Metal.h>

#include <sokol/sokol_gfx.h>

extern "C" unsigned long long hanabi_metal_allocated_bytes(void) {
    const void* raw = sg_mtl_device();
    if (raw == nullptr) return 0ull;
    id<MTLDevice> device = (__bridge id<MTLDevice>)raw;
    return static_cast<unsigned long long>([device currentAllocatedSize]);
}
