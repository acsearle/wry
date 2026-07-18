//
//  world_map.hpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#ifndef world_map_hpp
#define world_map_hpp

#include <memory>
#include <vector>

#include "atomic.hpp"
#include "world.hpp"

namespace wry {

    // A one-pixel-per-tile RGBA8 (sRGB bytes) image of the mapped world
    // region: terrain as the base layer in the shared TERRAIN_COLOR_SRGB
    // colors, with the sparse Term and entity-occupancy layers plotted over
    // it in deliberately un-natural colors.  Unmapped cells (no terrain
    // generated) stay transparent black.
    //
    // The mapped rect is fixed to the starting terrain region for now;
    // growing it belongs with lazy terrain generation.
    struct WorldMap {

        static constexpr int32_t EXTENT = 256;      // tiles per side, 1 px/tile
        static constexpr int32_t X0 = -EXTENT / 2;  // world x of pixel column 0
        static constexpr int32_t Y0 = -EXTENT / 2;  // world y of pixel row 0

        // Row-major, row j = world row y = Y0 + j (so texture v ~ world y
        // when uploaded rows-in-order).  EXTENT * EXTENT * 4 bytes.
        std::vector<uint8_t> rgba;

    };

    // Producer -> consumer handoff between the background build coroutine
    // (thread-pool worker) and the renderer (main thread), shared_ptr-held
    // by both so an in-flight build never dangles across a WorldState
    // teardown (the same lifetime idiom as save_game_async's result cell).
    //
    // Ordering: the builder publishes with finished.exchange_release, the
    // renderer consumes with take_finished's exchange_acquire; that
    // release/acquire pair is the synchronizes-with edge making the fully
    // written pixel buffer visible to the consumer.  in_flight is the
    // one-build-at-a-time gate: the builder's store_release(false) on
    // completion pairs with the main thread's load_acquire in the cadence
    // check, so a new build starts only after the previous publication.
    struct WorldMapHandoff {

        Atomic<WorldMap*> finished{nullptr};
        Atomic<bool> in_flight{false};

        // Main thread (renderer).  Caller owns the result.
        WorldMap* take_finished() {
            return finished.exchange_acquire(nullptr);
        }

        // Runs only after the last build dropped its shared_ptr, so a
        // plain (nonatomic) read of the slot is race-free here.
        ~WorldMapHandoff() {
            delete finished.nonatomic_load();
        }

    };

    // Spawn the background build over a rooted snapshot (the same
    // walk-a-frozen-snapshot contract as the async save: World::step never
    // mutates, it builds fresh worlds, so the walk reads stable structure
    // while play continues).  Anchored in the process WaitGroup; yields to
    // the pool between slices of work.
    void world_map_build_async(Root<const World*> snapshot,
                               std::shared_ptr<WorldMapHandoff> handoff);

} // namespace wry

#endif /* world_map_hpp */
