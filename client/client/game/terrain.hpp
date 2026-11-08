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
#include "matter.hpp"
#include "waitable_map.hpp"

namespace wry {

    // Which kind of ground occupies a tile.  Terrain is a dense spatial
    // layer below the Coordinate -> Term layer: mutable but rarely mutated,
    // present everywhere that has been generated, absent (not yet generated)
    // far from any entity.  Its one gameplay property so far is what
    // MINE and BURY make of it (TERRAIN_MINING below); impassable and
    // the like come later.
    //
    // Codes are save-format constants: append only, never renumber.

    using Terrain = int;

    inline constexpr Terrain TERRAIN_WATER = 0;
    inline constexpr Terrain TERRAIN_SAND  = 1;
    inline constexpr Terrain TERRAIN_GRASS = 2;
    inline constexpr Terrain TERRAIN_ROCK  = 3;
    inline constexpr Terrain TERRAIN_COAL      = 4;
    inline constexpr Terrain TERRAIN_IRON_ORE  = 5;
    inline constexpr Terrain TERRAIN_LIMESTONE = 6;

    inline constexpr int TERRAIN_KIND_COUNT = 7;

    // Representative solid color per kind, as sRGB bytes, indexed by
    // Terrain: the board's pastels (warm of their names: the sky's light
    // is blue).  The board (WryWorldScene.mm's WryTerrainUniforms tints)
    // and the world-map builder (1 px/tile) both ink from here, so the
    // map reads as the board seen from afar.
    inline constexpr uint8_t TERRAIN_COLOR_SRGB[TERRAIN_KIND_COUNT][4] = {
        { 216, 226, 234, 255 },   // TERRAIN_WATER
        { 244, 232, 200, 255 },   // TERRAIN_SAND
        { 224, 232, 206, 255 },   // TERRAIN_GRASS
        { 224, 220, 210, 255 },   // TERRAIN_ROCK
        { 150, 150, 158, 255 },   // TERRAIN_COAL: the one dark kind
        { 220, 172, 150, 255 },   // TERRAIN_IRON_ORE: rust
        { 238, 238, 230, 255 },   // TERRAIN_LIMESTONE: chalk, a step paler than rock
    };

    // What MINE takes out of (and BURY puts back into) each kind: the
    // ticks a dig takes (a hop is 64) and the matter it yields, a
    // matter.hpp MATTER code (or -1: none).  Every kind yields its own
    // matter, water included; that piling up water and mining grass is
    // odd is accepted for now.  Simulation-visible: peers must agree, so
    // these are save-compatibility constants like the codes themselves.
    struct TerrainMining {
        int64_t ticks;
        int32_t matter;
    };

    inline constexpr TerrainMining TERRAIN_MINING[TERRAIN_KIND_COUNT] = {
        {  64, MATTER_WATER     },   // TERRAIN_WATER
        { 128, MATTER_SAND      },   // TERRAIN_SAND
        { 192, MATTER_GRASS     },   // TERRAIN_GRASS
        { 512, MATTER_ROCK      },   // TERRAIN_ROCK
        { 320, MATTER_COAL      },   // TERRAIN_COAL
        { 448, MATTER_IRON_ORE  },   // TERRAIN_IRON_ORE
        { 384, MATTER_LIMESTONE },   // TERRAIN_LIMESTONE
    };

    // A dig where there is no terrain (or an unknown code): one hop's
    // worth of scraping at nothing, yielding nothing
    inline constexpr TerrainMining TERRAIN_MINING_NONE = { 64, -1 };

    constexpr TerrainMining terrain_mining(Terrain t) {
        return ((t >= 0) && (t < TERRAIN_KIND_COUNT))
            ? TERRAIN_MINING[t]
            : TERRAIN_MINING_NONE;
    }

    // Which of the 4 variants decorates this tile (the board's marks;
    // formerly the terrain material atlas's columns, assets/terrain/): an avalanche mix of the coordinates, so the
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

    // ---- Generation ----------------------------------------------------
    //
    // The terrain of a world is a pure function of its seed (World::_seed)
    // and the cell: bln.h noise fields (integer-only, bitwise identical on
    // every platform, and chunk-consistent: independently generated blocks
    // agree on shared cells) banded and thresholded per cell.  It is made
    // in chunks of TERRAIN_CHUNK^2 cells, aligned to multiples of
    // TERRAIN_CHUNK, on demand:
    //
    //   - make_starting_world makes the TERRAIN_STARTING_EXTENT^2 block
    //     centered on the origin (generate_starting_terrain);
    //   - each World step makes, before publishing the next World, every
    //     missing chunk in the 3x3 block around each cell an entity
    //     proposed to occupy that step (World::_advance).  Proposals are a
    //     deterministic function of the World, so every peer makes the
    //     same chunks at the same step, and the 3x3 block keeps at least
    //     one chunk of terrain ahead of anything moving one cell a step;
    //   - worldgen searches (the Ship's berth) may make chunks as they go.
    //
    // Once made, terrain is World state like any other: saved, hashed and
    // donated to a joining client, never regenerated.  A chunk is made
    // when its corner cell is absent; making it fills only absent cells,
    // so terrain set by hand (a pier, a test's fixture) stands.  A World
    // whose _seed is 0 (hand-made: tests, fixtures) never makes terrain.
    //
    // Cost: about 0.3 ms a chunk in the Debug + ASan build (terrain_chunk_
    // cost; four noise fields, each paying its octave pyramid's apron on a
    // small block), so a step makes its handful synchronously -- a machine
    // crossing into a new chunk column makes three.  The background
    // precompute of the frontier imagined in docs/todo.md waits for a
    // generator that is actually expensive.

    inline constexpr int32_t TERRAIN_CHUNK = 16;
    inline constexpr int32_t TERRAIN_STARTING_EXTENT = 256;

    // The seed of the starting world: elevation 20, deposits 840..842
    inline constexpr uint64_t WORLD_SEED_DEFAULT = 20;

    // The chunk index holding cell coordinate v (floor division)
    constexpr int32_t terrain_chunk_of(int32_t v) {
        return (v >= 0) ? (v / TERRAIN_CHUNK) : -((-v + TERRAIN_CHUNK - 1) / TERRAIN_CHUNK);
    }

    // The chunk (cx, cy) of the terrain of `seed`, row-major from its
    // south-west corner (cx * TERRAIN_CHUNK, cy * TERRAIN_CHUNK)
    void generate_terrain_chunk(uint64_t seed, int32_t cx, int32_t cy, Terrain* out);

    // Make chunk (cx, cy) in `terrain` if its corner cell is absent;
    // true if it did
    bool ensure_terrain_chunk(uint64_t seed, WaitableMap<Coordinate, Terrain>& terrain,
                              int32_t cx, int32_t cy);

    // Make the missing chunks of the 3x3 block around xy's chunk; how many
    int ensure_terrain_around(uint64_t seed, WaitableMap<Coordinate, Terrain>& terrain,
                              Coordinate xy);

    // Make the TERRAIN_STARTING_EXTENT^2 block centered on the origin from
    // world->_seed.  Caller owns the (unpublished, under-construction)
    // world; see make_starting_world.
    void generate_starting_terrain(World* world);

} // namespace wry

#endif /* terrain_hpp */
