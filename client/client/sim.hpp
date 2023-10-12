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
        
    
    enum DISCRIMINANT 
    : i64 {
        
        DISCRIMINANT_NUMBER   = 0,
        DISCRIMINANT_OPCODE   = 1,
        DISCRIMINANT_RESOURCE = 2,
        
    };
    
    // Given the complexity of minerals etc., can we reasonably simplify
    // chemistry down to any scheme that roughly matches real industrial
    // processes?  Or should we just have arbitrary IDs and recipes?
    
    // processes:
    //
    // milling
    // chloralkali
    // pyrometallurgy
    //   - calcination
    //   - roasting / pyrolisis
    //   - smelting
    // electrolysis (AlO)
    // leaching, precipitation
    
    enum ELEMENT
    : i64 {
        // universe
        HYDROGEN,
        HELIUM,
        OXYGEN,
        CARBON,
        NEON,
        IRON,
        NITROGEN,
        SILICON,
        MAGNESIUM,
        SULFUR,
        POTASSIUM,
        NICKEL,
        // humans
        PHOSPHORUS,
        CHLORINE,
        SODIUM,
        CALCIUM,
        // seawater
        BROMINE,
        BORON,
        FLUORINE,
        // crust
        ALUMINUM,
        TITANIUM,

        // notable but relatively rare
        LITHIUM,
        CHROMIUM,
        MANGANESE,
        COBALT,
        COPPER,
        ZINC,
        ARSENIC,
        SILVER,
        TIN,
        PLATINUM,
        GOLD,
        MERCURY,
        LEAD,
        URANIUM,
    };

    enum COMPOUND : i64 {
                
        WATER, // H2O
        
        // by crust abundance
        SILICON_DIOXIDE,
        
        // ( source)
        
    };
    
    enum OPCODE 
    : i64 {
        
        OPCODE_NOOP,
        OPCODE_SKIP,
        OPCODE_HALT,
        
        OPCODE_TURN_NORTH, // _heading:2 = 0   // clockwise
        OPCODE_TURN_EAST,  // _heading:2 = 1
        OPCODE_TURN_SOUTH, // _heading:2 = 2
        OPCODE_TURN_WEST,  // _heading:2 = 3
        
        OPCODE_TURN_RIGHT, // ++_heading
        OPCODE_TURN_LEFT,  // --_heading
        OPCODE_TURN_BACK,  // _heading += 2;
        
        OPCODE_BRANCH_RIGHT, // _heading += pop()
        OPCODE_BRANCH_LEFT, // _heading -= pop();
        
        OPCODE_LOAD,     // push([_location])
        OPCODE_STORE,    // [_location] = pop()
        OPCODE_EXCHANGE, // push(exchange([_location], pop())
        
        OPCODE_HEADING_LOAD, // push(_heading)
        OPCODE_HEADING_STORE, // _heading = pop()
        
        OPCODE_LOCATION_LOAD,  // push(_location)
        OPCODE_LOCATION_STORE, // _location = pop()
        
        OPCODE_DROP,      // pop()
        OPCODE_DUPLICATE, // a = pop(); push(a); push(a)
        OPCODE_OVER,      // a = pop(); b = pop(); push(b); push(a); push(b);
        OPCODE_SWAP,      // a = pop(); b = pop(); push(a); push(b);
        
        OPCODE_IS_ZERO,           // 010
        OPCODE_IS_POSITIVE,       // 001
        OPCODE_IS_NEGATIVE,       // 100
        OPCODE_IS_NOT_ZERO,       // 101
        OPCODE_IS_NOT_POSITIVE,   // 110
        OPCODE_IS_NOT_NEGATIVE,   // 011
        
        OPCODE_LOGICAL_NOT,
        OPCODE_LOGICAL_AND,
        OPCODE_LOGICAL_OR,
        OPCODE_LOGICAL_XOR,
        
        OPCODE_BITWISE_NOT,
        OPCODE_BITWISE_AND,
        OPCODE_BITWISE_OR,
        OPCODE_BITWISE_XOR,
        
        OPCODE_BITWISE_SPLIT,
        OPCODE_POPCOUNT,
        
        OPCODE_NEGATE,    // push(-pop())
        OPCODE_ABS,       // push(abs(pop()))
        OPCODE_SIGN,      // push(sign(pop()))
        
        OPCODE_ADD,       // a = pop(); b = pop(); push(b + a)
        OPCODE_SUBTRACT,  // a = pop(); b = pop(); push(b - a)
        
        OPCODE_EQUAL,         // a = pop(); b = pop(); push(b == a)
        OPCODE_NOT_EQUAL,     // a = pop(); b = pop(); push(b != a)
        OPCODE_LESS_THAN,     // a = pop(); b = pop(); push(b - a)
        OPCODE_GREATER_THAN,
        OPCODE_LESS_THAN_OR_EQUAL_TO,
        OPCODE_GREATER_THAN_OR_EQUAL_TO, // a <= b
        OPCODE_COMPARE, // sign(a - b)
        
    };
    
    
    
    enum HEADING 
    : i64 {
        
        HEADING_NORTH = 0,
        HEADING_EAST = 1,
        HEADING_SOUTH = 2,
        HEADING_WEST = 3,
        HEADING_MASK = 3
        
    };

    struct World;
    struct Entity;
    
    struct Coordinate {
        
        i32 x;
        i32 y;
        
        bool operator==(const Coordinate&) const = default;
        
    }; // struct Coordinate
    
    inline u64 hash(const Coordinate& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    using Time = i64;
    
    struct Value {
        
        i64 discriminant;
        i64 value;
        
        operator bool() const {
            return value;
        }
        
        bool operator!() const {
            return !value;
        }
        
        bool operator==(const Value&) const = default;
        
        
    }; // struct Value

} // namespace wry::sim



#endif /* sim_hpp */
