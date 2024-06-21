//
//  array.cpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#include "array.hpp"

#include "gc.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("HeapArray") {
        
        gc::mutator_enter();
        
        gc::HeapArray* a = new gc::HeapArray();
        
        for (int i = 0; i != 100; ++i) {
            assert(a->empty() == !i);
            assert(a->size() == i);
            a->push_back(i);
            assert(a->size() == i + 1);
            assert(a->back() == i);
            assert(a->front() == 0);
        }
        
        for (int i = 100; i--;) {
            assert(a->empty() == i);
            assert(a->size() == i+1);
            assert(a->back() == i);
            assert(a->front() == 0);
            a->pop_back();
            assert(a->size() == i);
        }
        
        gc::mutator_leave();
        
    };
    
}

