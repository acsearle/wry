//
//  terrain.cpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#include <cassert>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

#include "terrain.hpp"

#include "bln.h"
#include "test.hpp"
#include "world.hpp"

namespace wry {

    namespace {

        // One field over the block, from these params
        std::vector<int32_t> terrain_field(const bln_params& params,
                                           int64_t x0, int64_t y0,
                                           int32_t w, int32_t h) {
            std::vector<int32_t> field((size_t)w * (size_t)h);
            std::vector<int64_t> scratch(bln_scratch_size(&params, w, h)
                                         / sizeof(int64_t));
            int rc = bln_generate(&params, x0, y0, w, h,
                                  field.data(), w, scratch.data());
            assert(rc == 0);
            (void)rc;
            return field;
        }

    } // anonymous namespace

    namespace {

        // The block [x0, x0 + w) x [y0, y0 + h) of the terrain of `seed`,
        // row-major from (x0, y0).  A pure function of (seed, x, y) per
        // cell: bln.h fields agree bitwise across independently generated
        // blocks, and the classification below is per cell, so a chunk
        // equals the same cells cut from any larger block.
        void generate_terrain_block(uint64_t seed,
                                    int64_t x0, int64_t y0,
                                    int32_t w, int32_t h,
                                    Terrain* out) {
            // The terrain is a partition of a four-dimensional parameter
            // space sampled at every tile: elevation, and a deposit strength
            // for each of the three ores.

            // Elevation: one fBm field, thresholded into bands.  weights grow
            // by 2^1 per octave toward the coarse end (H = 1), so the
            // coarsest octave dominates and the map reads as a few
            // continents/oceans at the 256-tile scale with detail at every
            // finer scale.
            bln_params elevation = {};
            elevation.seed = (uint32_t)seed;   // 20 (WORLD_SEED_DEFAULT) puts the origin on
                                                // open grass with coast/mountain/lake landmarks in view
            elevation.levels = 6;
            elevation.passes = 2;
            elevation.out_shift = 6;   // hard-safe: ceil(log2(sum weights = 63))
            for (int k = 0; k != elevation.levels; ++k)
                elevation.weights[k] = (uint16_t)(1u << k);

            // Deposits: one field per ore, independently seeded, of shorter
            // range than elevation (four octaves, the coarsest two equal, so
            // patches are ~5-20 tiles across rather than continents).  Where
            // a field's upper tail clears the threshold the ore claims the
            // land (grass or rock; water and beaches keep their kind), and
            // where several do the strongest wins, so patches abut rather
            // than ringing each other the way bands of one shared field would.
            constexpr int ore_count = 3;
            constexpr Terrain ore_kind[ore_count] = {
                TERRAIN_COAL, TERRAIN_IRON_ORE, TERRAIN_LIMESTONE,
            };
            bln_params deposit[ore_count] = {};
            for (int n = 0; n != ore_count; ++n) {
                // 42 * seed + n: for the default seed 20 these are 840, 841,
                // 842, chosen (search over 3b, 3b+1, 3b+2) so that a patch of
                // each ore lies in the starting view, none within three tiles
                // of the origin
                deposit[n].seed = (uint32_t)(42u * (uint32_t)seed + (uint32_t)n);
                deposit[n].levels = 4;
                deposit[n].passes = 2;
                deposit[n].out_shift = 4;   // hard-safe: ceil(log2(1+2+4+4 = 11))
                constexpr uint16_t octave_weight[4] = { 1, 2, 4, 4 };
                for (int k = 0; k != deposit[n].levels; ++k)
                    deposit[n].weights[k] = octave_weight[k];
            }

            std::vector<int32_t> height = terrain_field(elevation, x0, y0, w, h);
            std::vector<int32_t> ore[ore_count];
            for (int n = 0; n != ore_count; ++n)
                ore[n] = terrain_field(deposit[n], x0, y0, w, h);

            // Band thresholds in field units.  Measured over this region and
            // parameter set: elevation sigma ~ 1.5e8; these cuts give ~22%
            // water, ~11% sand (a beach ring around every water body), ~57%
            // grass, ~10% rock, with the central 32x32 all land.  The deposit
            // threshold (~2 sigma of a deposit field) then claims ~2.1% of
            // the block as coal, ~2.9% iron ore and ~1.8% limestone, out of
            // the grass and rock.
            constexpr int32_t deposit_threshold = 200000000;
            for (int32_t j = 0; j != h; ++j) {
                for (int32_t i = 0; i != w; ++i) {
                    size_t k = (size_t)j * (size_t)w + (size_t)i;
                    int32_t v = height[k];
                    Terrain t = v < -160000000 ? TERRAIN_WATER
                              : v < -110000000 ? TERRAIN_SAND
                              : v < +150000000 ? TERRAIN_GRASS
                              :                  TERRAIN_ROCK;
                    if (t == TERRAIN_GRASS || t == TERRAIN_ROCK) {
                        int32_t strongest = deposit_threshold;
                        for (int n = 0; n != ore_count; ++n) {
                            if (ore[n][k] > strongest) {
                                strongest = ore[n][k];
                                t = ore_kind[n];
                            }
                        }
                    }
                    out[k] = t;
                }
            }
        }

    } // anonymous namespace

    void generate_terrain_chunk(uint64_t seed, int32_t cx, int32_t cy, Terrain* out) {
        generate_terrain_block(seed,
                               (int64_t)cx * TERRAIN_CHUNK, (int64_t)cy * TERRAIN_CHUNK,
                               TERRAIN_CHUNK, TERRAIN_CHUNK, out);
    }

    bool ensure_terrain_chunk(uint64_t seed, WaitableMap<Coordinate, Terrain>& terrain,
                              int32_t cx, int32_t cy) {
        const Coordinate corner{cx * TERRAIN_CHUNK, cy * TERRAIN_CHUNK};
        Terrain t = -1;
        if (terrain.try_get(corner, t))
            return false;
        Terrain cells[TERRAIN_CHUNK * TERRAIN_CHUNK];
        generate_terrain_chunk(seed, cx, cy, cells);
        for (int32_t j = 0; j != TERRAIN_CHUNK; ++j) {
            for (int32_t i = 0; i != TERRAIN_CHUNK; ++i) {
                Coordinate xy{corner.x + i, corner.y + j};
                // never overwrite: a cell set by hand (a pier, a test)
                // stands
                if (!terrain.try_get(xy, t))
                    terrain.set(xy, cells[j * TERRAIN_CHUNK + i]);
            }
        }
        return true;
    }

    int ensure_terrain_around(uint64_t seed, WaitableMap<Coordinate, Terrain>& terrain,
                              Coordinate xy) {
        const int32_t cx = terrain_chunk_of(xy.x);
        const int32_t cy = terrain_chunk_of(xy.y);
        int made = 0;
        for (int32_t j = cy - 1; j <= cy + 1; ++j)
            for (int32_t i = cx - 1; i <= cx + 1; ++i)
                made += ensure_terrain_chunk(seed, terrain, i, j);
        return made;
    }

    void generate_starting_terrain(World* world) {
        constexpr int32_t R = TERRAIN_STARTING_EXTENT / TERRAIN_CHUNK / 2;
        for (int32_t cy = -R; cy != R; ++cy)
            for (int32_t cx = -R; cx != R; ++cx)
                (void) ensure_terrain_chunk(world->_seed, world->_terrain_for_coordinate, cx, cy);
    }

    // The partition's measured character, pinned: every kind present in
    // roughly its documented share, the origin's neighbourhood open
    // grass, and a patch of each ore in the starting view (the window a
    // 1920 x 1080 window shows at the default zoom, about 25 x 15 tiles).
    // A change to the generator that breaks one of these should be a
    // decision, not an accident.
    define_test("terrain_starting_kinds") {
        World* w = new World;
        w->_seed = WORLD_SEED_DEFAULT;
        generate_starting_terrain(w);
        int count[TERRAIN_KIND_COUNT] = {};
        int near[TERRAIN_KIND_COUNT] = {};
        for (i32 y = -128; y != 128; ++y) {
            for (i32 x = -128; x != 128; ++x) {
                Terrain t = -1;
                bool has = w->_terrain_for_coordinate.try_get(Coordinate{x, y}, t);
                assert(has);
                assert((t >= 0) && (t < TERRAIN_KIND_COUNT));
                ++count[t];
                if ((-12 <= x) && (x <= 12) && (-7 <= y) && (y <= 7))
                    ++near[t];
                if ((-3 <= x) && (x <= 3) && (-3 <= y) && (y <= 3))
                    assert(t == TERRAIN_GRASS);
            }
        }
        for (Terrain t = 0; t != TERRAIN_KIND_COUNT; ++t)
            assert(count[t] > 256 * 256 / 100);   // over 1% each
        for (Terrain t : { TERRAIN_COAL, TERRAIN_IRON_ORE, TERRAIN_LIMESTONE })
            assert(near[t] >= 4);
        co_return;
    };

    // Chunks are cut from one field: chunk (cx, cy) made alone equals the
    // same cells of the starting block, at both signs of both axes, and
    // terrain_chunk_of floors.  A seed is a different world.
    define_test("terrain_chunk_consistency") {
        static_assert(terrain_chunk_of(0) == 0);
        static_assert(terrain_chunk_of(15) == 0);
        static_assert(terrain_chunk_of(16) == 1);
        static_assert(terrain_chunk_of(-1) == -1);
        static_assert(terrain_chunk_of(-16) == -1);
        static_assert(terrain_chunk_of(-17) == -2);
        World* w = new World;
        w->_seed = WORLD_SEED_DEFAULT;
        generate_starting_terrain(w);
        Terrain cells[TERRAIN_CHUNK * TERRAIN_CHUNK];
        int differ = 0;
        for (auto [cx, cy] : { std::pair{0, 0}, {-1, -1}, {3, -5}, {-8, 7}, {7, -8} }) {
            generate_terrain_chunk(WORLD_SEED_DEFAULT, cx, cy, cells);
            for (int32_t j = 0; j != TERRAIN_CHUNK; ++j)
                for (int32_t i = 0; i != TERRAIN_CHUNK; ++i) {
                    Terrain t = -1;
                    bool has = w->_terrain_for_coordinate.try_get(
                        Coordinate{cx * TERRAIN_CHUNK + i, cy * TERRAIN_CHUNK + j}, t);
                    assert(has);
                    assert(t == cells[j * TERRAIN_CHUNK + i]);
                }
            Terrain other[TERRAIN_CHUNK * TERRAIN_CHUNK];
            generate_terrain_chunk(WORLD_SEED_DEFAULT + 1, cx, cy, other);
            for (int k = 0; k != TERRAIN_CHUNK * TERRAIN_CHUNK; ++k)
                differ += (other[k] != cells[k]);
        }
        assert(differ > 0);
        // making an existing chunk is a no-op, and never overwrites
        w->_terrain_for_coordinate.set(Coordinate{20 * TERRAIN_CHUNK + 3, 4}, TERRAIN_ROCK);
        assert(!ensure_terrain_chunk(WORLD_SEED_DEFAULT, w->_terrain_for_coordinate, 0, 0));
        assert(ensure_terrain_chunk(WORLD_SEED_DEFAULT, w->_terrain_for_coordinate, 20, 0));
        Terrain t = -1;
        assert(w->_terrain_for_coordinate.try_get(Coordinate{20 * TERRAIN_CHUNK + 3, 4}, t));
        assert(t == TERRAIN_ROCK);
        co_return;
    };

    // What a chunk costs (printed, not asserted): the number terrain.hpp
    // quotes when it says generation is cheap enough to do in the step.
    define_test("terrain_chunk_cost") {
        Terrain cells[TERRAIN_CHUNK * TERRAIN_CHUNK];
        constexpr int N = 64;
        auto t0 = std::chrono::steady_clock::now();
        int sink = 0;
        for (int n = 0; n != N; ++n) {
            generate_terrain_chunk(WORLD_SEED_DEFAULT, 1000 + n, -500, cells);
            sink += cells[n];
        }
        auto t1 = std::chrono::steady_clock::now();
        printf("terrain_chunk_cost: %.1f us per %dx%d chunk (%d)\n",
               std::chrono::duration<double, std::micro>(t1 - t0).count() / N,
               TERRAIN_CHUNK, TERRAIN_CHUNK, sink & 1);
        co_return;
    };

} // namespace wry
