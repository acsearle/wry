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
        Atomic<int> b;
        b.add_fetch(8, Ordering::RELAXED);
        b.min_fetch(4, Ordering::RELAXED);
        int x = 99;
        b.wait(x, Ordering::RELAXED);
        b.notify_one();
        co_return;
    };
    
}
