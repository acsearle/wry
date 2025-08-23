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

namespace wry::sim {
    
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

} // namespace wry::sim

#endif /* opcode_hpp */
