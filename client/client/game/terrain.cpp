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
#include "world.hpp"

namespace wry {

    void generate_starting_terrain(World* world) {

        // One fBm elevation field, thresholded into bands.  weights grow by
        // 2^1 per octave toward the coarse end (H = 1), so the coarsest
        // octave dominates and the map reads as a few continents/oceans at
        // the 256-tile scale with detail at every finer scale.
        bln_params params = {};
        params.seed = 20;   // chosen so the origin sits on open grass with
                            // coast/mountain/lake landmarks in view
        params.levels = 6;
        params.passes = 2;
        params.out_shift = 6;   // hard-safe: ceil(log2(sum weights = 63))
        for (int k = 0; k != params.levels; ++k)
            params.weights[k] = (uint16_t)(1u << k);

        constexpr int32_t extent = 256;
        constexpr int64_t x0 = -extent / 2;
        constexpr int64_t y0 = -extent / 2;

        std::vector<int32_t> field((size_t)extent * (size_t)extent);
        std::vector<int64_t> scratch(bln_scratch_size(&params, extent, extent)
                                     / sizeof(int64_t));
        int rc = bln_generate(&params, x0, y0, extent, extent,
                              field.data(), extent, scratch.data());
        assert(rc == 0);
        (void)rc;

        // Band thresholds in field units.  Measured over this region and
        // parameter set: sigma ~ 1.5e8; these cuts give ~22% water, ~11%
        // sand (a beach ring around every water body), ~57% grass, ~10%
        // rock, with the central 32x32 all land.
        for (int32_t j = 0; j != extent; ++j) {
            for (int32_t i = 0; i != extent; ++i) {
                int32_t v = field[(size_t)j * (size_t)extent + (size_t)i];
                Terrain t = v < -160000000 ? TERRAIN_WATER
                          : v < -110000000 ? TERRAIN_SAND
                          : v < +150000000 ? TERRAIN_GRASS
                          :                  TERRAIN_ROCK;
                world->_terrain_for_coordinate.set(
                    Coordinate{(i32)(x0 + i), (i32)(y0 + j)}, t);
            }
        }
    }

} // namespace wry
