//
//  value.cpp
//  client
//
//  Created by Antony Searle on 19/1/2024.
//

#include "value.hpp"
#include "test.hpp"

namespace wry::value {
    
    define_test("value") {
        
        Value a(1);
        Value b(2);
        Value c = a + b;
        
        assert(a == a);
        assert(b == b);
        assert(a != b);
        assert(a == Value(1));
        assert(b - a == a);
        
        
    };
    
}
