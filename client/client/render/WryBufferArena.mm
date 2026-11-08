//
//  WryBufferArena.mm
//  client
//
//  Created by Antony Searle on 2026-10-11.
//

#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

#include "WryBufferArena.h"
#include "transient_arena.hpp"

@implementation WryBufferArena
{
    id<MTLDevice> _device;
    id<MTLSharedEvent> _event;
    wry::TransientArenaCore _core;
    std::vector<id<MTLBuffer>> _buffers;  // one per _core.chunks entry
    std::thread::id _owner;               // render thread, set on first use
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    if ((self = [super init])) {
        _device = device;
        _event = [device newSharedEvent];
        _event.label = @"Transient arena timeline";
    }
    return self;
}

- (void)assertOwner {
    if (_owner == std::thread::id{})
        _owner = std::this_thread::get_id();
    assert(_owner == std::this_thread::get_id());
}

- (WryBufferSlice)allocate:(NSUInteger)length {
    [self assertOwner];
    const auto a = _core.allocate(length, _event.signaledValue);
    while (_buffers.size() <= a.chunk) {
        const std::size_t i = _buffers.size();
        id<MTLBuffer> b = [_device newBufferWithLength:_core.chunks[i].length
                                               options:MTLResourceStorageModeShared];
        b.label = [NSString stringWithFormat:@"Transient arena chunk %zu", i];
        // Rare after warm-up (chunks are recycled, never released); a
        // steady stream of these would mean frames are not completing.
        NSLog(@"Transient arena: chunk %zu, %zu KiB", i, _core.chunks[i].length >> 10);
        _buffers.push_back(b);
    }
    id<MTLBuffer> b = _buffers[a.chunk];
    return WryBufferSlice{ b, a.offset, (char*)b.contents + a.offset };
}

- (WryBufferSlice)copyBytes:(const void*)bytes length:(NSUInteger)length {
    WryBufferSlice s = [self allocate:length];
    if (length)
        std::memcpy(s.contents, bytes, length);
    return s;
}

// The ordering edges reuse relies on, for a chunk stamped N:
//   - CPU writes to a slice (shared storage) precede -commit on this
//     thread, and commit makes them visible to the command buffer's reads.
//   - The signal is encoded after the frame's last encoder has ended, so
//     the GPU sets the event to N only after every command before it in
//     the command buffer -- every draw reading the chunk -- has completed.
//   - -allocate: polls signaledValue on the render thread; observing a
//     value >= N therefore orders that frame's GPU reads before the next
//     CPU write into the chunk (a write-after-read).  The GPU never writes
//     these chunks, so no visibility edge back to the CPU is needed.
// Testing stamp <= signaledValue (not ==) takes a later frame's value as
// covering every earlier frame.  That assumes one queue's frame buffers
// finish (and so signal) in commit order, which is how Metal schedules
// a single queue's command buffers; we rely on it rather than on any
// documented cross-buffer guarantee of encodeSignalEvent.
- (void)endFrameOnCommandBuffer:(id<MTLCommandBuffer>)commandBuffer {
    [self assertOwner];
    [commandBuffer encodeSignalEvent:_event value:_core.end_frame()];
}

@end
