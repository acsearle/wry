//
//  sim.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef sim_hpp
#define sim_hpp

#include "stdint.hpp"
#include "hash.hpp"

namespace wry::sim {
    
    // simple enum reflection
    
    struct ENUM_PAIR {
        i64 first;
        const char* second;
    };
    
    // define opcodes
    
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

    enum OPCODE
    : i64 {
        
#define X(Y) Y
        
        WRY_OPCODES_X
        
#undef X
        
    };
    
    inline constexpr ENUM_PAIR OPCODE_NAMES[] = {
        
#define X(Y) { Y, #Y }
        
        WRY_OPCODES_X
        
#undef X
        
    };
    
#undef WRY_OPCODES_X
    
    
    using Time = i64;
    
    // These can't be found by ADL since
    inline void trace(const Time&) {}
    inline void shade(const Time&) {}
    
}

#include "value.hpp"

namespace wry::sim {
    
    
    enum HEADING
    : i64 {
        
        HEADING_NORTH = 0,
        HEADING_EAST = 1,
        HEADING_SOUTH = 2,
        HEADING_WEST = 3,
        HEADING_MASK = 3
        
    };


    struct Coordinate {
        
        i32 x;
        i32 y;
        
        constexpr bool operator==(const Coordinate&) const = default;
        constexpr auto operator<=>(const Coordinate&) const = default;
        
    }; // struct Coordinate
    
    inline u64 hash(const Coordinate& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    inline void trace(const Coordinate&) {}
    inline void shade(const Coordinate&) {}

    using gc::Value;
    
    enum TRANSACTION_STATE {
        
        TRANSACTION_STATE_NONE = 0,
        TRANSACTION_STATE_READ = 1,
        TRANSACTION_STATE_WRITE = 2,
        TRANSACTION_STATE_FORBIDDEN = 3,
        
    };
    
    
    struct World;
    struct Entity;
    struct PersistentWorld;
    struct Transaction;
    struct TransactionSet;
    struct EntityID;
    
    struct EntityID {
        uint64_t data;
        
        auto operator<=>(const EntityID&) const = default;
        
    };
    
    inline void trace(const EntityID&) {}
    
    


    
} // namespace wry::sim



#endif /* sim_hpp */
