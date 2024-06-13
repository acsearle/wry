//
//  atomic.cpp
//  client
//
//  Created by Antony Searle on 11/6/2024.
//

#include "atomic.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("Atomic") {
        using namespace gc;
        Atomic<int> b;
        b.add_fetch(8, Order::RELAXED);
        b.min_fetch(4, Order::RELAXED);
        int x = b.wait(99, Order::RELAXED);
        b.notify_one();
    };
    
}
