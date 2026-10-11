//
//  transient_arena.cpp
//  client
//
//  Created by Antony Searle on 2026-10-11.
//

#include <random>

#include "transient_arena.hpp"
#include "test.hpp"

namespace wry {

    // Brute force against a simulated GPU.  Every allocation is recorded
    // with its frame's stamp; the simulated timeline lags the CPU by a
    // random 0..kMaxLag frames (monotone, sometimes stalling).  Each new
    // allocation must be aligned, inside its chunk, and disjoint from every
    // allocation the GPU may still be reading -- those of frames stamped
    // above the completed value, and of the current frame.
    define_test("transient_arena") {
        using Core = TransientArenaCore;
        constexpr std::size_t C = Core::kChunkSize;

        struct Live {
            std::uint64_t stamp;
            std::size_t chunk;
            std::size_t begin;
            std::size_t end;
        };

        Core core;
        std::vector<Live> live;
        std::mt19937_64 rng(12345);
        std::uint64_t completed = 0;
        std::uint64_t frame = 1;  // stamp of the frame being built
        constexpr std::uint64_t kMaxLag = 3;

        auto check_and_record = [&](std::size_t size, std::size_t alignment) {
            Core::Allocation a = core.allocate(size, completed, alignment);
            assert(a.chunk < core.chunks.size());
            assert(a.offset % alignment == 0);
            assert(a.offset <= core.chunks[a.chunk].length);
            assert(size <= core.chunks[a.chunk].length - a.offset);
            for (Live const& l : live) {
                if (l.stamp <= completed || l.chunk != a.chunk)
                    continue;
                assert(a.offset + size <= l.begin || l.end <= a.offset);
            }
            live.push_back(Live{ frame, a.chunk, a.offset, a.offset + size });
        };

        auto end_frame = [&]() {
            const std::uint64_t stamp = core.end_frame();
            assert(stamp == frame);
            ++frame;
            // Advance the simulated GPU: monotone, at most kMaxLag behind.
            const std::uint64_t floor = (frame - 1 > kMaxLag) ? frame - 1 - kMaxLag : 0;
            std::uint64_t target = completed + (rng() % 3);  // 0 = stall
            target = std::clamp(target, std::max(completed, floor), frame - 1);
            completed = target;
            std::erase_if(live, [&](Live const& l) { return l.stamp <= completed; });
        };

        // Phase 1: random workload, including zero-size, chunk-straddling
        // and larger-than-chunk requests.
        const std::size_t alignments[] = { 4, 16, 256 };
        for (int f = 0; f != 2000; ++f) {
            const int n = (int)(rng() % 40);
            for (int k = 0; k != n; ++k) {
                std::size_t size;
                switch (rng() % 10) {
                    case 0:  size = 0; break;
                    case 1:  size = C / 2 + rng() % C; break;      // straddles
                    case 2:  size = (rng() % 4 == 0) ? C * 2 + rng() % C : rng() % 4096; break;
                    default: size = rng() % 65536; break;
                }
                check_and_record(size, alignments[rng() % 3]);
            }
            end_frame();
        }

        // Phase 2: a fixed workload under the maximum lag reaches a steady
        // state: after warm-up no new chunks are ever created.
        {
            auto fixed_frame = [&]() {
                for (int k = 0; k != 10; ++k)
                    check_and_record(C / 3, 256);
                check_and_record(C * 3, 256);
                const std::uint64_t stamp = core.end_frame();
                assert(stamp == frame);
                ++frame;
                completed = (frame - 1 > kMaxLag) ? frame - 1 - kMaxLag : 0;
                std::erase_if(live, [&](Live const& l) { return l.stamp <= completed; });
            };
            for (int f = 0; f != 2 * (int)kMaxLag + 4; ++f)
                fixed_frame();
            const std::size_t steady = core.chunks.size();
            for (int f = 0; f != 100; ++f)
                fixed_frame();
            assert(core.chunks.size() == steady);
        }

        co_return;
    };

} // namespace wry
