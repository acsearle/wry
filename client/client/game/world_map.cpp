//
//  world_map.cpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#include <cstring>

#include "world_map.hpp"

#include "coroutine.hpp"
#include "terrain.hpp"
#include "test.hpp"
#include "waitable_map.hpp"

namespace wry {

    namespace {

        // Layer colors over the natural terrain palette: deliberately
        // un-natural so the sparse content pops at map scale.
        constexpr uint8_t MAP_TERM_SRGB[4]   = { 255,   0, 255, 255 };  // magenta
        constexpr uint8_t MAP_ENTITY_SRGB[4] = { 255, 255, 255, 255 };  // white

        void plot(WorldMap& map, Coordinate xy, const uint8_t (&srgb)[4]) {
            size_t i = (size_t)(xy.x - WorldMap::X0);
            size_t j = (size_t)(xy.y - WorldMap::Y0);
            std::memcpy(map.rgba.data() + (j * WorldMap::EXTENT + i) * 4,
                        srgb, 4);
        }

    } // anonymous namespace

    // Not parallel on purpose: this is a ~1 Hz background amenity that
    // should cost much less than a core, so it runs as one coroutine that
    // periodically yields back to the pool (each slice is a fraction of a
    // millisecond of AMT descent).  It runs on pool workers, which are
    // pinned mutators, so reading the snapshot's GC structure is safe.
    static Coroutine::Task world_map_build(Root<const World*> snapshot,
                                           std::shared_ptr<WorldMapHandoff> handoff) {

        constexpr int32_t E = WorldMap::EXTENT;
        constexpr int32_t X0 = WorldMap::X0;
        constexpr int32_t Y0 = WorldMap::Y0;

        WorldMap* map = new WorldMap;
        map->rgba.assign((size_t)E * (size_t)E * 4, 0);  // transparent = unmapped

        const World* world = &*snapshot;

        // Base layer: terrain, in horizontal slices with a yield between
        // each so the build never hogs a worker.
        constexpr int32_t SLICE_ROWS = 16;
        for (int32_t j = 0; j != E; j += SLICE_ROWS) {
            visit_in_region(world->_terrain_for_coordinate,
                            Coordinate{X0, Y0 + j},
                            Coordinate{X0 + E - 1, Y0 + j + SLICE_ROWS - 1},
                            [map](Coordinate xy, Terrain t) {
                if ((t < 0) || (t >= TERRAIN_KIND_COUNT))
                    t = TERRAIN_KIND_COUNT - 1;
                plot(*map, xy, TERRAIN_COLOR_SRGB[t]);
            });
            co_await Coroutine::SuspendAndSchedule{};
        }

        // Sparse layers, one visit each: any Term on the ground, then any
        // occupying entity on top (a travelling machine claims both its
        // endpoint cells, so it shows as a two-pixel blip).
        visit_in_region(world->_term_for_coordinate,
                        Coordinate{X0, Y0}, Coordinate{X0 + E - 1, Y0 + E - 1},
                        [map](Coordinate xy, Term) {
            plot(*map, xy, MAP_TERM_SRGB);
        });
        co_await Coroutine::SuspendAndSchedule{};

        visit_in_region(world->_entity_id_for_coordinate,
                        Coordinate{X0, Y0}, Coordinate{X0 + E - 1, Y0 + E - 1},
                        [map](Coordinate xy, EntityID id) {
            if (id)
                plot(*map, xy, MAP_ENTITY_SRGB);
        });

        // Publish (see WorldMapHandoff for the ordering contract).  A
        // superseded, never-consumed build is deleted here.
        WorldMap* superseded = handoff->finished.exchange_release(map);
        delete superseded;
        handoff->in_flight.store_release(false);

        // Fall off the end: the frame (and the Root snapshot) is destroyed
        // on a pool worker, which is a mutator, as ~Root requires.
        co_return;
    }

    void world_map_build_async(Root<const World*> snapshot,
                               std::shared_ptr<WorldMapHandoff> handoff) {
        wait_group_spawn(world_map_build(std::move(snapshot), std::move(handoff)));
    }

    // End-to-end build over a hand-made world: pins the pixel/world
    // orientation contract (row j = world y = Y0 + j), the layer order
    // (entity over term over terrain), rect clipping, the unmapped =
    // transparent convention, and the in_flight/finished handoff protocol.
    define_test("world_map_build") {

        World* w = new World;

        // Terrain at the rect's extreme corners and interior; entries just
        // outside the rect must not plot (and must not corrupt neighbors).
        w->_terrain_for_coordinate.set(Coordinate{0, 0}, TERRAIN_GRASS);
        w->_terrain_for_coordinate.set(Coordinate{-128, -128}, TERRAIN_WATER);
        w->_terrain_for_coordinate.set(Coordinate{127, 127}, TERRAIN_ROCK);
        w->_terrain_for_coordinate.set(Coordinate{5, 0}, TERRAIN_SAND);
        w->_terrain_for_coordinate.set(Coordinate{6, 1}, TERRAIN_SAND);
        w->_terrain_for_coordinate.set(Coordinate{128, 0}, TERRAIN_ROCK);
        w->_terrain_for_coordinate.set(Coordinate{-129, 0}, TERRAIN_ROCK);
        w->_terrain_for_coordinate.set(Coordinate{0, 128}, TERRAIN_ROCK);

        // A Term over sand, and an occupant over sand: layers plot on top.
        w->_term_for_coordinate.set(Coordinate{5, 0}, term_make_integer_with(7));
        w->_entity_id_for_coordinate.set(Coordinate{6, 1}, EntityID{42});

        auto handoff = std::make_shared<WorldMapHandoff>();
        handoff->in_flight.store_relaxed(true);
        world_map_build_async(Root<const World*>{w}, handoff);

        // The builder yields between slices; polling with the same
        // scheduler lets it interleave.  Wall-clock bound, as in the async
        // save test.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (handoff->in_flight.load_acquire()
               && (std::chrono::steady_clock::now() < deadline))
            co_await Coroutine::SuspendAndSchedule{};
        assert(!handoff->in_flight.load_acquire());

        WorldMap* m = handoff->take_finished();
        assert(m);
        assert(m->rgba.size() == (size_t)WorldMap::EXTENT * WorldMap::EXTENT * 4);

        auto px = [m](int32_t x, int32_t y) -> const uint8_t* {
            size_t i = (size_t)(x - WorldMap::X0);
            size_t j = (size_t)(y - WorldMap::Y0);
            return m->rgba.data() + (j * WorldMap::EXTENT + i) * 4;
        };
        auto is = [](const uint8_t* p, const uint8_t (&c)[4]) {
            return std::memcmp(p, c, 4) == 0;
        };

        assert(is(px(0, 0), TERRAIN_COLOR_SRGB[TERRAIN_GRASS]));
        assert(is(px(-128, -128), TERRAIN_COLOR_SRGB[TERRAIN_WATER]));
        assert(is(px(127, 127), TERRAIN_COLOR_SRGB[TERRAIN_ROCK]));
        assert(is(px(5, 0), MAP_TERM_SRGB));      // term over sand
        assert(is(px(6, 1), MAP_ENTITY_SRGB));    // entity over sand
        assert(px(100, -100)[3] == 0);            // unmapped: transparent
        assert(px(127, 0)[3] == 0);               // {128,0} clipped, no wrap
        assert(px(-128, 0)[3] == 0);              // {-129,0} clipped, no wrap
        assert(px(0, 127)[3] == 0);               // {0,128} clipped, no wrap

        delete m;
        co_return;
    };

} // namespace wry
