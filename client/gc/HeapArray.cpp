//
//  wry/HeapArray.cpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#include "HeapArray.hpp"

#include "gc.hpp"
#include "test.hpp"
#include "value.hpp"

namespace wry::gc {
    
    define_test("HeapArray") {
        
        mutator_enter();
        
       auto* a = new GCArray<Scan<Value>>();
        
        assert(a->empty() == true);
        assert(a->size() == 0);
        
        for (int i = 0; i != 100; ++i) {
            assert(a->empty() == !i);
            assert(a->size() == i);
            a->push_back(i);
            assert(a->size() == i + 1);
            assert(a->back() == i);
            assert(a->front() == 0);
        }
        
        for (int i = 100; i--;) {
            assert(a->empty() == false);
            assert(a->size() == i+1);
            assert(a->back() == i);
            assert(a->front() == 0);
            a->pop_back();
            assert(a->size() == i);
        }
        
        assert(a->empty() == true);
        assert(a->size() == 0);

        mutator_leave();
        
    };
    
}

