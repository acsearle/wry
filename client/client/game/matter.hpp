//
//  matter.hpp
//  client
//
//  Created by Antony Searle on 5/7/2026.
//

#ifndef matter_hpp
#define matter_hpp

#include <cstdint>
#include <utility>
#include <cstring>
#include <iterator>

namespace wry {

    // Matter codes: the payload of an ENUMERATION(meta=MATTER) Term,
    // identifying a kind of physical object.
    //
    // Matter Terms are conserved.  They are fungible values (all
    // shipping containers are the same bit pattern); what is conserved
    // is the count of each kind.  Machines move matter -- LOAD takes it
    // from a cell, STORE waits for the destination and places it, DROP
    // puts it down ahead, EXCHANGE swaps -- but never copy or destroy
    // it; DUPLICATE and OVER refuse matter, and arithmetic ignores it.
    // Matter enters and leaves the world only at its edges: Source
    // entities create it and Sink entities destroy it, a Ship sells it
    // (destroying it for MATTER_PRICE money), and the ground
    // itself is both -- MINE digs a terrain kind's matter out of the
    // ground (creating it) and BURY returns matching matter to it
    // (destroying it); see terrain.hpp TERRAIN_MINING and the gates in
    // machine.cpp.
    //
    // The terrain matters (MATTER_WATER .. MATTER_LIMESTONE) are what
    // MINE digs out of each Terrain kind, in Terrain-code order.
    //
    // Codes are save-format constants: append only, never renumber.

#define WRY_MATTER_X \
X(MATTER_SHIPPING_CONTAINER),\
X(MATTER_WATER),\
X(MATTER_SAND),\
X(MATTER_GRASS),\
X(MATTER_ROCK),\
X(MATTER_COAL),\
X(MATTER_IRON_ORE),\
X(MATTER_LIMESTONE),\

    enum MATTER
    : int32_t {

#define X(Y) Y

        WRY_MATTER_X

#undef X

    };

    constexpr std::pair<MATTER, const char*> MATTER_NAMES[] = {

#define X(Y) { Y, #Y }

        WRY_MATTER_X

#undef X

    };

#undef WRY_MATTER_X

    constexpr MATTER MATTER_from_name(const char* name) {
        for (std::size_t i = 0; i != std::size(MATTER_NAMES); ++i) {
            if (!std::strcmp(MATTER_NAMES[i].second, name)) {
                return MATTER_NAMES[i].first;
            }
        }
        return MATTER{-1};
    }

    // Representative colour of each kind's pile (the sand-art box the
    // board draws on a matter tile; it multiplies the box's light grey
    // layers), as sRGB bytes indexed by MATTER code.
    // Renderer-side cosmetics only -- never simulation state.
    inline constexpr uint8_t MATTER_COLOR_SRGB[][4] = {
        { 200, 200, 200, 255 },   // MATTER_SHIPPING_CONTAINER (drawn as its mesh)
        { 110, 160, 220, 255 },   // MATTER_WATER
        { 232, 208, 150, 255 },   // MATTER_SAND
        { 120, 180,  90, 255 },   // MATTER_GRASS
        { 165, 160, 150, 255 },   // MATTER_ROCK
        {  60,  60,  66, 255 },   // MATTER_COAL
        { 185,  98,  62, 255 },   // MATTER_IRON_ORE
        { 240, 236, 222, 255 },   // MATTER_LIMESTONE
    };
    static_assert(std::size(MATTER_COLOR_SRGB) == std::size(MATTER_NAMES));

    // What a Ship pays for each kind, indexed by MATTER code: whole
    // dollars, added to World::_money as the Ship consumes it.  Zero is
    // unpriced: a Ship refuses it, leaving it in the hold (so a water
    // load blocks that hold until somebody takes it back out).
    // Simulation state: changing a price changes the game's history.
    inline constexpr int64_t MATTER_PRICE[] = {
         0,   // MATTER_SHIPPING_CONTAINER
         0,   // MATTER_WATER
         2,   // MATTER_SAND
         1,   // MATTER_GRASS
         3,   // MATTER_ROCK
         8,   // MATTER_COAL
        12,   // MATTER_IRON_ORE
         5,   // MATTER_LIMESTONE
    };
    static_assert(std::size(MATTER_PRICE) == std::size(MATTER_NAMES));

    constexpr int64_t matter_price(int code) {
        return ((code >= 0) && ((std::size_t)code < std::size(MATTER_PRICE)))
            ? MATTER_PRICE[code]
            : 0;
    }

    constexpr const char* name_from_MATTER(MATTER value) {
        for (std::size_t i = 0; i != std::size(MATTER_NAMES); ++i) {
            if (MATTER_NAMES[i].first == value) {
                return MATTER_NAMES[i].second;
            }
        }
        return nullptr;
    }

} // namespace wry

#endif /* matter_hpp */
