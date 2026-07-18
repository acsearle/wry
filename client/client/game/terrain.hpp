//
//  terrain.hpp
//  client
//
//  Created by Antony Searle on 18/7/2026.
//

#ifndef terrain_hpp
#define terrain_hpp

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

    struct World;

    // Fill a 256x256 block of terrain centered on the origin from a
    // deterministic band-limited noise field (bln.h).  Same params in, same
    // terrain out, bitwise, on every platform -- terrain generation must be
    // reproducible for lockstep multiplayer and late-join.  Caller owns the
    // (unpublished, under-construction) world; see make_starting_world.
    void generate_starting_terrain(World* world);

} // namespace wry

#endif /* terrain_hpp */
