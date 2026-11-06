//
//  terrain.cpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#include <cassert>
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
                                           int32_t extent) {
            std::vector<int32_t> field((size_t)extent * (size_t)extent);
            std::vector<int64_t> scratch(bln_scratch_size(&params, extent, extent)
                                         / sizeof(int64_t));
            int rc = bln_generate(&params, x0, y0, extent, extent,
                                  field.data(), extent, scratch.data());
            assert(rc == 0);
            (void)rc;
            return field;
        }

    } // anonymous namespace

    void generate_starting_terrain(World* world) {

        // The terrain is a partition of a four-dimensional parameter
        // space sampled at every tile: elevation, and a deposit strength
        // for each of the three ores.

        // Elevation: one fBm field, thresholded into bands.  weights grow
        // by 2^1 per octave toward the coarse end (H = 1), so the
        // coarsest octave dominates and the map reads as a few
        // continents/oceans at the 256-tile scale with detail at every
        // finer scale.
        bln_params elevation = {};
        elevation.seed = 20;   // chosen so the origin sits on open grass
                               // with coast/mountain/lake landmarks in view
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
            // seeds chosen (search over 3b, 3b+1, 3b+2) so that a patch
            // of each ore lies in the starting view, none within three
            // tiles of the origin
            deposit[n].seed = (uint32_t)(840 + n);
            deposit[n].levels = 4;
            deposit[n].passes = 2;
            deposit[n].out_shift = 4;   // hard-safe: ceil(log2(1+2+4+4 = 11))
            constexpr uint16_t w[4] = { 1, 2, 4, 4 };
            for (int k = 0; k != deposit[n].levels; ++k)
                deposit[n].weights[k] = w[k];
        }

        constexpr int32_t extent = 256;
        constexpr int64_t x0 = -extent / 2;
        constexpr int64_t y0 = -extent / 2;

        std::vector<int32_t> height = terrain_field(elevation, x0, y0, extent);
        std::vector<int32_t> ore[ore_count];
        for (int n = 0; n != ore_count; ++n)
            ore[n] = terrain_field(deposit[n], x0, y0, extent);

        // Band thresholds in field units.  Measured over this region and
        // parameter set: elevation sigma ~ 1.5e8; these cuts give ~22%
        // water, ~11% sand (a beach ring around every water body), ~57%
        // grass, ~10% rock, with the central 32x32 all land.  The deposit
        // threshold (~2 sigma of a deposit field) then claims ~2.1% of
        // the block as coal, ~2.9% iron ore and ~1.8% limestone, out of
        // the grass and rock.
        constexpr int32_t deposit_threshold = 200000000;
        for (int32_t j = 0; j != extent; ++j) {
            for (int32_t i = 0; i != extent; ++i) {
                size_t k = (size_t)j * (size_t)extent + (size_t)i;
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
                world->_terrain_for_coordinate.set(
                    Coordinate{(i32)(x0 + i), (i32)(y0 + j)}, t);
            }
        }
    }

    // The partition's measured character, pinned: every kind present in
    // roughly its documented share, the origin's neighbourhood open
    // grass, and a patch of each ore in the starting view (the window a
    // 1920 x 1080 window shows at the default zoom, about 25 x 15 tiles).
    // A change to the generator that breaks one of these should be a
    // decision, not an accident.
    define_test("terrain_starting_kinds") {
        World* w = new World;
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

} // namespace wry
