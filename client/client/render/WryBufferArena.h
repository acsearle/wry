//
//  WryBufferArena.h
//  client
//
//  Created by Antony Searle on 2026-10-11.
//

#ifndef WryBufferArena_h
#define WryBufferArena_h

#import <Metal/Metal.h>

// Per-device allocator for per-frame transient GPU data: everything the CPU
// writes for one frame's command buffer to read (sprite / GUI quads, mesh
// instance arrays, per-frame geometry).  Slices of large shared MTLBuffers,
// bump-allocated; see transient_arena.hpp for the bookkeeping.
//
// Lifetime is gated by an MTLSharedEvent used as a timeline: the frame's
// command buffer signals the frame's value on completion (via
// -endFrameOnCommandBuffer:), and a chunk is reused only once the event has
// reached the value it was retired with.  Reclamation is polled when
// allocating -- no completion handlers -- which is the shape Vulkan
// timeline semaphores and D3D12 fences also have.
//
// Contract: allocate only for the frame command buffer that the next
// -endFrameOnCommandBuffer: is called on, and only from the render thread.
// Write the slice's contents before that command buffer is committed.

NS_ASSUME_NONNULL_BEGIN

struct WryBufferSlice {
    id<MTLBuffer> buffer;
    NSUInteger offset;
    void* contents;  // CPU address of the slice (buffer.contents + offset)
};

@interface WryBufferArena : NSObject

- (instancetype)initWithDevice:(id<MTLDevice>)device;

// `length` bytes aligned to 256 (enough for any buffer binding).
- (WryBufferSlice)allocate:(NSUInteger)length;

// Allocate and fill.
- (WryBufferSlice)copyBytes:(const void*)bytes length:(NSUInteger)length;

// Close the frame: encode the signal its chunks' reuse waits on.  Call
// once per frame, after the frame's last encoder has ended and before the
// command buffer is committed.
- (void)endFrameOnCommandBuffer:(id<MTLCommandBuffer>)commandBuffer;

@end

NS_ASSUME_NONNULL_END

#endif /* WryBufferArena_h */
