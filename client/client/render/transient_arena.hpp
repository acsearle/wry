//
//  transient_arena.hpp
//  client
//
//  Created by Antony Searle on 2026-10-11.
//
//  Bookkeeping for a per-frame transient GPU allocator (vertex / instance /
//  constant data written by the CPU each frame and read by the GPU once).
//  Metal-free: the platform wrapper (WryBufferArena) owns one GPU buffer
//  per chunk this core describes, and a GPU timeline counter
//  (MTLSharedEvent; a Vulkan timeline semaphore or D3D12 fence would serve
//  equally) that the GPU advances as frames complete.
//
//  Shape: memory comes in chunks; allocation bumps a cursor through the
//  current chunk.  Transient data all dies at the frame boundary, so there
//  is no per-allocation free and hence no fit problem: whole chunks are
//  retired at end_frame, stamped with the timeline value that frame's
//  command buffer signals on completion, and become reusable once the
//  counter has reached their stamp.  Reclamation is polled at acquire,
//  on the allocating thread -- no completion callbacks.
//
//  Chunks are kChunkSize, except that a request too big for one gets a
//  dedicated chunk of the next power of two; such chunks join the same
//  free list, and acquisition takes the smallest free chunk that fits.
//  Chunks are never released (steady-state memory is the high-water mark
//  of frames in flight times per-frame use).
//
//  Single-threaded: allocate / end_frame from the render thread only.

#ifndef transient_arena_hpp
#define transient_arena_hpp

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wry {

    struct TransientArenaCore {

        static constexpr std::size_t kChunkSize = std::size_t(4) << 20;
        static constexpr std::size_t kDefaultAlignment = 256;

        struct Chunk {
            std::size_t length;
            // Timeline value whose completion frees the chunk; meaningful
            // only while retired.
            std::uint64_t stamp = 0;
        };

        struct Allocation {
            std::size_t chunk;   // index into chunks; may be newly created
            std::size_t offset;  // aligned
        };

        std::vector<Chunk> chunks;

        // Allocate `size` bytes aligned to `alignment` (a power of two).
        // `completed` is the timeline's current value (everything stamped
        // at or below it is no longer read by the GPU).  If the result
        // names a chunk index the caller has not seen, the caller must
        // create its backing of chunks[index].length bytes.
        Allocation allocate(std::size_t size,
                            std::uint64_t completed,
                            std::size_t alignment = kDefaultAlignment) {
            assert(std::has_single_bit(alignment));
            if (_has_current) {
                const std::size_t offset = align_up(_cursor, alignment);
                if (offset <= chunks[_current].length
                    && size <= chunks[_current].length - offset) {
                    _cursor = offset + size;
                    return Allocation{ _current, offset };
                }
            }
            _current = acquire(size, completed);
            _has_current = true;
            _used.push_back(_current);
            _cursor = size;
            return Allocation{ _current, 0 };
        }

        // Close the frame: retire every chunk it used, stamped with the
        // returned value, which the caller must have the frame's command
        // buffer signal on completion.  Values start at 1 and increase by
        // one per frame (the timeline starts at 0).
        std::uint64_t end_frame() {
            const std::uint64_t stamp = _next_stamp++;
            for (std::size_t i : _used) {
                chunks[i].stamp = stamp;
                _retired.push_back(i);
            }
            _used.clear();
            _has_current = false;
            _cursor = 0;
            return stamp;
        }

        // Introspection, for tests.
        std::size_t free_count() const { return _free.size(); }
        std::size_t retired_count() const { return _retired.size(); }

    private:

        static std::size_t align_up(std::size_t x, std::size_t a) {
            return (x + a - 1) & ~(a - 1);
        }

        std::size_t acquire(std::size_t size, std::uint64_t completed) {
            // Reclaim retired chunks the GPU has finished with.
            std::erase_if(_retired, [&](std::size_t i) {
                if (chunks[i].stamp > completed)
                    return false;
                _free.push_back(i);
                return true;
            });
            // Smallest free chunk that fits.
            auto best = _free.end();
            for (auto it = _free.begin(); it != _free.end(); ++it)
                if (chunks[*it].length >= size
                    && (best == _free.end()
                        || chunks[*it].length < chunks[*best].length))
                    best = it;
            if (best != _free.end()) {
                const std::size_t i = *best;
                *best = _free.back();
                _free.pop_back();
                return i;
            }
            chunks.push_back(Chunk{ std::max(kChunkSize, std::bit_ceil(size)) });
            return chunks.size() - 1;
        }

        std::vector<std::size_t> _free;
        std::vector<std::size_t> _retired;
        std::vector<std::size_t> _used;    // this frame, including current
        std::size_t _current = 0;
        bool _has_current = false;
        std::size_t _cursor = 0;
        std::uint64_t _next_stamp = 1;
    };

} // namespace wry

#endif /* transient_arena_hpp */
