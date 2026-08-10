//
//  opcode.hpp
//  client
//
//  Created by Antony Searle on 22/12/2024.
//

#ifndef opcode_hpp
#define opcode_hpp

#include <cstdint>
#include <array>
#include <utility>
#include <cstring>

namespace wry {

    // define opcodes
    //
    // Opcode values are save-format constants: append only, never
    // renumber or reorder.

#define WRY_OPCODES_X \
X(OPCODE_NOOP),\
X(OPCODE_SKIP),\
X(OPCODE_HALT),\
X(OPCODE_TURN_NORTH),\
X(OPCODE_TURN_EAST),\
X(OPCODE_TURN_SOUTH),\
X(OPCODE_TURN_WEST),\
X(OPCODE_TURN_RIGHT),\
X(OPCODE_TURN_LEFT),\
X(OPCODE_TURN_BACK),\
X(OPCODE_BRANCH_RIGHT),\
X(OPCODE_BRANCH_LEFT),\
X(OPCODE_LOAD),\
X(OPCODE_STORE),\
X(OPCODE_EXCHANGE),\
X(OPCODE_HEADING_LOAD),\
X(OPCODE_HEADING_STORE),\
X(OPCODE_LOCATION_LOAD),\
X(OPCODE_LOCATION_STORE),\
X(OPCODE_DROP),\
X(OPCODE_DUPLICATE),\
X(OPCODE_SWAP),\
X(OPCODE_OVER),\
X(OPCODE_IS_ZERO),\
X(OPCODE_IS_POSITIVE),\
X(OPCODE_IS_NEGATIVE),\
X(OPCODE_IS_NOT_ZERO),\
X(OPCODE_IS_NOT_POSITIVE),\
X(OPCODE_IS_NOT_NEGATIVE),\
X(OPCODE_LOGICAL_NOT),\
X(OPCODE_LOGICAL_AND),\
X(OPCODE_LOGICAL_OR),\
X(OPCODE_LOGICAL_XOR),\
X(OPCODE_BITWISE_NOT),\
X(OPCODE_BITWISE_AND),\
X(OPCODE_BITWISE_OR),\
X(OPCODE_BITWISE_XOR),\
X(OPCODE_BITWISE_SPLIT),\
X(OPCODE_SHIFT_RIGHT),\
X(OPCODE_POPCOUNT),\
X(OPCODE_ABS),\
X(OPCODE_NEGATE),\
X(OPCODE_SIGN),\
X(OPCODE_ADD),\
X(OPCODE_SUBTRACT),\
X(OPCODE_EQUAL),\
X(OPCODE_NOT_EQUAL),\
X(OPCODE_LESS_THAN),\
X(OPCODE_GREATER_THAN),\
X(OPCODE_LESS_THAN_OR_EQUAL_TO),\
X(OPCODE_GREATER_THAN_OR_EQUAL_TO),\
X(OPCODE_COMPARE),\
X(OPCODE_FLIP_FLOP),\
X(OPCODE_FLOP_FLIP),\
X(OPCODE_ROT),\
X(OPCODE_IS_MATTER),\
X(OPCODE_VALVE_NORTH_SOUTH),\
X(OPCODE_VALVE_EAST_WEST),\
X(OPCODE_DO_NOT_QUEUE),\

    enum OPCODE
    : int64_t {
        
#define X(Y) Y
        
        WRY_OPCODES_X
        
#undef X
        
    };
    
    constexpr std::pair<OPCODE, const char*> OPCODE_NAMES[] = {
        
#define X(Y) { Y, #Y }
        
        WRY_OPCODES_X
        
#undef X
        
    };
    
#undef WRY_OPCODES_X
    
    constexpr OPCODE OPCODE_from_name(const char* name) {
        for (std::size_t i = 0; i != std::size(OPCODE_NAMES); ++i) {
            if (!std::strcmp(OPCODE_NAMES[i].second, name)) {
                return OPCODE_NAMES[i].first;
            }
        }
        return OPCODE{-1};
    }

    constexpr const char* name_from_OPCODE(OPCODE value) {
        for (std::size_t i = 0; i != std::size(OPCODE_NAMES); ++i) {
            if (OPCODE_NAMES[i].first == value) {
                return OPCODE_NAMES[i].second;
            }
        }
        return nullptr;
    }

    // ====================================================================
    // Reorientation: how each opcode transforms when a block of program
    // is rotated or mirrored (Factorio-style R / SHIFT-R / H / V).
    //
    // One master table with columns (opcode, rotated-clockwise,
    // mirrored-horizontal); everything else derives.  Counterclockwise
    // is the inverse of clockwise; mirror-vertical is rotate-twice then
    // mirror-horizontal (the 180-degree rotation is central, so the
    // composition order is immaterial).  MIRROR HORIZONTAL means
    // east <-> west (reflection across the north-south axis); MIRROR
    // VERTICAL means north <-> south.
    //
    // The behaviours that appear:
    //   invariant        heading-relative or heading-free opcodes --
    //                    almost everything, including the memory ops
    //                    (their operand cell reorients with the machine)
    //   direction-like   the absolute TURNs: a 4-cycle under rotation,
    //                    each mirror fixing its axis pair
    //   axis-like        the VALVEs: a 2-cycle under rotation,
    //                    invariant under both mirrors
    //   handedness-like  TURN/BRANCH LEFT/RIGHT and FLIP_FLOP/FLOP_FLIP:
    //                    invariant under rotation, swapped by any mirror
    //
    // CAUTION -- the dishonest identities: HEADING_LOAD, HEADING_STORE
    // (and the unimplemented LOCATION pair) map to themselves because
    // nothing else is possible, but they are NOT honestly transformable:
    // they traffic in absolute heading codes as DATA -- integers on the
    // ground and on stacks -- which no glyph table can reach.  A
    // reoriented block containing them (or containing integer literals
    // used as heading codes for BRANCH-as-computed-switch) changes
    // meaning.  Relative-turn data is safe: BRANCH_RIGHT n mirrors to
    // BRANCH_LEFT n, which is correct.  See machine_language.md.

#define WRY_OPCODE_REORIENTATIONS_X \
X(OPCODE_NOOP,                     OPCODE_NOOP,                     OPCODE_NOOP),\
X(OPCODE_SKIP,                     OPCODE_SKIP,                     OPCODE_SKIP),\
X(OPCODE_HALT,                     OPCODE_HALT,                     OPCODE_HALT),\
X(OPCODE_TURN_NORTH,               OPCODE_TURN_EAST,                OPCODE_TURN_NORTH),\
X(OPCODE_TURN_EAST,                OPCODE_TURN_SOUTH,               OPCODE_TURN_WEST),\
X(OPCODE_TURN_SOUTH,               OPCODE_TURN_WEST,                OPCODE_TURN_SOUTH),\
X(OPCODE_TURN_WEST,                OPCODE_TURN_NORTH,               OPCODE_TURN_EAST),\
X(OPCODE_TURN_RIGHT,               OPCODE_TURN_RIGHT,               OPCODE_TURN_LEFT),\
X(OPCODE_TURN_LEFT,                OPCODE_TURN_LEFT,                OPCODE_TURN_RIGHT),\
X(OPCODE_TURN_BACK,                OPCODE_TURN_BACK,                OPCODE_TURN_BACK),\
X(OPCODE_BRANCH_RIGHT,             OPCODE_BRANCH_RIGHT,             OPCODE_BRANCH_LEFT),\
X(OPCODE_BRANCH_LEFT,              OPCODE_BRANCH_LEFT,              OPCODE_BRANCH_RIGHT),\
X(OPCODE_LOAD,                     OPCODE_LOAD,                     OPCODE_LOAD),\
X(OPCODE_STORE,                    OPCODE_STORE,                    OPCODE_STORE),\
X(OPCODE_EXCHANGE,                 OPCODE_EXCHANGE,                 OPCODE_EXCHANGE),\
X(OPCODE_HEADING_LOAD,             OPCODE_HEADING_LOAD,             OPCODE_HEADING_LOAD),\
X(OPCODE_HEADING_STORE,            OPCODE_HEADING_STORE,            OPCODE_HEADING_STORE),\
X(OPCODE_LOCATION_LOAD,            OPCODE_LOCATION_LOAD,            OPCODE_LOCATION_LOAD),\
X(OPCODE_LOCATION_STORE,           OPCODE_LOCATION_STORE,           OPCODE_LOCATION_STORE),\
X(OPCODE_DROP,                     OPCODE_DROP,                     OPCODE_DROP),\
X(OPCODE_DUPLICATE,                OPCODE_DUPLICATE,                OPCODE_DUPLICATE),\
X(OPCODE_SWAP,                     OPCODE_SWAP,                     OPCODE_SWAP),\
X(OPCODE_OVER,                     OPCODE_OVER,                     OPCODE_OVER),\
X(OPCODE_IS_ZERO,                  OPCODE_IS_ZERO,                  OPCODE_IS_ZERO),\
X(OPCODE_IS_POSITIVE,              OPCODE_IS_POSITIVE,              OPCODE_IS_POSITIVE),\
X(OPCODE_IS_NEGATIVE,              OPCODE_IS_NEGATIVE,              OPCODE_IS_NEGATIVE),\
X(OPCODE_IS_NOT_ZERO,              OPCODE_IS_NOT_ZERO,              OPCODE_IS_NOT_ZERO),\
X(OPCODE_IS_NOT_POSITIVE,          OPCODE_IS_NOT_POSITIVE,          OPCODE_IS_NOT_POSITIVE),\
X(OPCODE_IS_NOT_NEGATIVE,          OPCODE_IS_NOT_NEGATIVE,          OPCODE_IS_NOT_NEGATIVE),\
X(OPCODE_LOGICAL_NOT,              OPCODE_LOGICAL_NOT,              OPCODE_LOGICAL_NOT),\
X(OPCODE_LOGICAL_AND,              OPCODE_LOGICAL_AND,              OPCODE_LOGICAL_AND),\
X(OPCODE_LOGICAL_OR,               OPCODE_LOGICAL_OR,               OPCODE_LOGICAL_OR),\
X(OPCODE_LOGICAL_XOR,              OPCODE_LOGICAL_XOR,              OPCODE_LOGICAL_XOR),\
X(OPCODE_BITWISE_NOT,              OPCODE_BITWISE_NOT,              OPCODE_BITWISE_NOT),\
X(OPCODE_BITWISE_AND,              OPCODE_BITWISE_AND,              OPCODE_BITWISE_AND),\
X(OPCODE_BITWISE_OR,               OPCODE_BITWISE_OR,               OPCODE_BITWISE_OR),\
X(OPCODE_BITWISE_XOR,              OPCODE_BITWISE_XOR,              OPCODE_BITWISE_XOR),\
X(OPCODE_BITWISE_SPLIT,            OPCODE_BITWISE_SPLIT,            OPCODE_BITWISE_SPLIT),\
X(OPCODE_SHIFT_RIGHT,              OPCODE_SHIFT_RIGHT,              OPCODE_SHIFT_RIGHT),\
X(OPCODE_POPCOUNT,                 OPCODE_POPCOUNT,                 OPCODE_POPCOUNT),\
X(OPCODE_ABS,                      OPCODE_ABS,                      OPCODE_ABS),\
X(OPCODE_NEGATE,                   OPCODE_NEGATE,                   OPCODE_NEGATE),\
X(OPCODE_SIGN,                     OPCODE_SIGN,                     OPCODE_SIGN),\
X(OPCODE_ADD,                      OPCODE_ADD,                      OPCODE_ADD),\
X(OPCODE_SUBTRACT,                 OPCODE_SUBTRACT,                 OPCODE_SUBTRACT),\
X(OPCODE_EQUAL,                    OPCODE_EQUAL,                    OPCODE_EQUAL),\
X(OPCODE_NOT_EQUAL,                OPCODE_NOT_EQUAL,                OPCODE_NOT_EQUAL),\
X(OPCODE_LESS_THAN,                OPCODE_LESS_THAN,                OPCODE_LESS_THAN),\
X(OPCODE_GREATER_THAN,             OPCODE_GREATER_THAN,             OPCODE_GREATER_THAN),\
X(OPCODE_LESS_THAN_OR_EQUAL_TO,    OPCODE_LESS_THAN_OR_EQUAL_TO,    OPCODE_LESS_THAN_OR_EQUAL_TO),\
X(OPCODE_GREATER_THAN_OR_EQUAL_TO, OPCODE_GREATER_THAN_OR_EQUAL_TO, OPCODE_GREATER_THAN_OR_EQUAL_TO),\
X(OPCODE_COMPARE,                  OPCODE_COMPARE,                  OPCODE_COMPARE),\
X(OPCODE_FLIP_FLOP,                OPCODE_FLIP_FLOP,                OPCODE_FLOP_FLIP),\
X(OPCODE_FLOP_FLIP,                OPCODE_FLOP_FLIP,                OPCODE_FLIP_FLOP),\
X(OPCODE_ROT,                      OPCODE_ROT,                      OPCODE_ROT),\
X(OPCODE_IS_MATTER,                OPCODE_IS_MATTER,                OPCODE_IS_MATTER),\
X(OPCODE_VALVE_NORTH_SOUTH,        OPCODE_VALVE_EAST_WEST,          OPCODE_VALVE_NORTH_SOUTH),\
X(OPCODE_VALVE_EAST_WEST,          OPCODE_VALVE_NORTH_SOUTH,        OPCODE_VALVE_EAST_WEST),\
X(OPCODE_DO_NOT_QUEUE,             OPCODE_DO_NOT_QUEUE,             OPCODE_DO_NOT_QUEUE),\

    constexpr std::pair<OPCODE, OPCODE> ROTATE_CLOCKWISE_OPCODE[] = {

#define X(Y, R, H) { Y, R }

        WRY_OPCODE_REORIENTATIONS_X

#undef X

    };

    constexpr std::pair<OPCODE, OPCODE> MIRROR_HORIZONTAL_OPCODE[] = {

#define X(Y, R, H) { Y, H }

        WRY_OPCODE_REORIENTATIONS_X

#undef X

    };

#undef WRY_OPCODE_REORIENTATIONS_X

    constexpr OPCODE opcode_rotated_clockwise(OPCODE x) {
        for (auto& [from, to] : ROTATE_CLOCKWISE_OPCODE)
            if (from == x)
                return to;
        return x;   // unknown opcodes reorient as themselves
    }

    constexpr OPCODE opcode_rotated_counterclockwise(OPCODE x) {
        // the inverse of a bijection: search the second column
        for (auto& [from, to] : ROTATE_CLOCKWISE_OPCODE)
            if (to == x)
                return from;
        return x;
    }

    constexpr OPCODE opcode_mirrored_horizontal(OPCODE x) {
        for (auto& [from, to] : MIRROR_HORIZONTAL_OPCODE)
            if (from == x)
                return to;
        return x;
    }

    constexpr OPCODE opcode_mirrored_vertical(OPCODE x) {
        return opcode_rotated_clockwise(
                   opcode_rotated_clockwise(
                       opcode_mirrored_horizontal(x)));
    }

    // The canonical representative of x's orbit under the full dihedral
    // group: the least enum value reachable by rotations and mirrors.
    // The palette shows one glyph per orbit; R / SHIFT-R / H / V reach
    // the rest.
    constexpr OPCODE opcode_orbit_representative(OPCODE x) {
        OPCODE best = x;
        OPCODE r = x;
        for (int i = 0; i != 4; ++i) {
            r = opcode_rotated_clockwise(r);
            if (r < best) best = r;
            OPCODE m = opcode_mirrored_horizontal(r);
            if (m < best) best = m;
        }
        return best;
    }

    constexpr bool opcode_is_orbit_representative(OPCODE x) {
        return opcode_orbit_representative(x) == x;
    }

    // Compile-time verification: the master table covers every opcode,
    // and the transforms have the dihedral group structure -- rotate^4
    // and mirror^2 are the identity, and the inverse lookup inverts.
    static_assert(std::size(ROTATE_CLOCKWISE_OPCODE) == std::size(OPCODE_NAMES));
    static_assert([]{
        for (auto& [op, name] : OPCODE_NAMES) {
            OPCODE r = op;
            for (int i = 0; i != 4; ++i)
                r = opcode_rotated_clockwise(r);
            if (r != op)
                return false;
            if (opcode_mirrored_horizontal(opcode_mirrored_horizontal(op)) != op)
                return false;
            if (opcode_mirrored_vertical(opcode_mirrored_vertical(op)) != op)
                return false;
            if (opcode_rotated_counterclockwise(opcode_rotated_clockwise(op)) != op)
                return false;
        }
        return true;
    }());
    static_assert(opcode_rotated_clockwise(OPCODE_TURN_NORTH) == OPCODE_TURN_EAST);
    static_assert(opcode_rotated_counterclockwise(OPCODE_TURN_NORTH) == OPCODE_TURN_WEST);
    static_assert(opcode_mirrored_horizontal(OPCODE_TURN_EAST) == OPCODE_TURN_WEST);
    static_assert(opcode_mirrored_vertical(OPCODE_TURN_NORTH) == OPCODE_TURN_SOUTH);
    static_assert(opcode_mirrored_vertical(OPCODE_TURN_EAST) == OPCODE_TURN_EAST);
    static_assert(opcode_rotated_clockwise(OPCODE_TURN_RIGHT) == OPCODE_TURN_RIGHT);
    static_assert(opcode_mirrored_horizontal(OPCODE_BRANCH_RIGHT) == OPCODE_BRANCH_LEFT);
    static_assert(opcode_mirrored_vertical(OPCODE_FLIP_FLOP) == OPCODE_FLOP_FLIP);
    static_assert(opcode_rotated_clockwise(OPCODE_VALVE_NORTH_SOUTH) == OPCODE_VALVE_EAST_WEST);
    static_assert(opcode_mirrored_horizontal(OPCODE_VALVE_NORTH_SOUTH) == OPCODE_VALVE_NORTH_SOUTH);
    static_assert(opcode_is_orbit_representative(OPCODE_TURN_NORTH));
    static_assert(!opcode_is_orbit_representative(OPCODE_TURN_WEST));
    static_assert(!opcode_is_orbit_representative(OPCODE_FLOP_FLIP));
    static_assert(!opcode_is_orbit_representative(OPCODE_VALVE_EAST_WEST));

} // namespace wry

#endif /* opcode_hpp */
