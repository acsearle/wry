//
//  terrain.hpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#ifndef terrain_hpp
#define terrain_hpp

#include <cstdint>

#include "coordinate.hpp"

namespace wry {

    // Which kind of ground occupies a tile.  Terrain is a dense spatial
    // layer below the Coordinate -> Term layer: mutable but rarely mutated,
    // present everywhere that has been generated, absent (not yet generated)
    // far from any entity.  For now terrain is decorative; gameplay
    // properties (impassable, mine-able, ...) come later.
    //
    // Codes are save-format constants: append only, never renumber.

    using Terrain = int;

    inline constexpr Terrain TERRAIN_WATER = 0;
    inline constexpr Terrain TERRAIN_SAND  = 1;
    inline constexpr Terrain TERRAIN_GRASS = 2;
    inline constexpr Terrain TERRAIN_ROCK  = 3;

    inline constexpr int TERRAIN_KIND_COUNT = 4;

    // Representative solid color per kind, as sRGB bytes, indexed by
    // Terrain.  Used by the world-map builder (1 px/tile) and echoed by
    // the synthesized water tile in assets/terrain; the in-world tiles
    // themselves now render from the terrain material atlases.
    inline constexpr uint8_t TERRAIN_COLOR_SRGB[TERRAIN_KIND_COUNT][4] = {
        {  58, 110, 165, 255 },   // TERRAIN_WATER
        { 214, 194, 138, 255 },   // TERRAIN_SAND
        { 104, 148,  76, 255 },   // TERRAIN_GRASS
        { 128, 124, 118, 255 },   // TERRAIN_ROCK
    };

    // Which variant column of the terrain material atlas (4 materials by
    // row = Terrain code, 4 variants by column; see assets/terrain/)
    // decorates this tile: an avalanche mix of the coordinates, so the
    // choice is deterministic, roughly uniform, and neighbor-decorrelated.
    // Renderer-side cosmetics only -- never simulation state.
    inline int terrain_variant(Coordinate xy) {
        uint32_t h = (uint32_t)xy.x * 0x9E3779B9u
                   ^ (uint32_t)xy.y * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return (int)(h & 3u);
    }

    struct World;

    // Fill a 256x256 block of terrain centered on the origin from a
    // deterministic band-limited noise field (bln.h).  Same params in, same
    // terrain out, bitwise, on every platform -- terrain generation must be
    // reproducible for lockstep multiplayer and late-join.  Caller owns the
    // (unpublished, under-construction) world; see make_starting_world.
    void generate_starting_terrain(World* world);

} // namespace wry

#endif /* terrain_hpp */
