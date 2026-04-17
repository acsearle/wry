//
//  array.cpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#include "contiguous_deque.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("contiguous_deque") {
        
        ContiguousDeque<int> a;
        
        assert(a.is_empty());
        assert(a.size() == 0);
        
        a.push_back(1);
        assert(!a.is_empty());
        assert(a.size() == 1);
        assert(a.front() == 1);
        assert(&a.front() == a.data());
        assert(a.begin() == a.data());
        assert(a.end() - a.begin() == a.size());
        assert(&a.back() == a.data());
        
        a.push_back(2);
        assert(!a.is_empty());
        assert(a.size() == 2);
        assert(a.front() == 1);
        assert(a.back() == 2);
        
        a.push_front(3);
        assert(a[0] == 3);
        assert(a[1] == 1);
        assert(a[2] == 2);
        
        a.clear();
        assert(a.is_empty());
        co_return;
    };
    
    
} // namespace wry
