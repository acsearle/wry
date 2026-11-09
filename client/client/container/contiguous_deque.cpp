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
        
        require(a.is_empty());
        require(a.size() == 0);
        
        a.push_back(1);
        require(!a.is_empty());
        require(a.size() == 1);
        require(a.front() == 1);
        require(&a.front() == a.data());
        require(a.begin() == a.data());
        require(a.end() - a.begin() == a.size());
        require(&a.back() == a.data());
        
        a.push_back(2);
        require(!a.is_empty());
        require(a.size() == 2);
        require(a.front() == 1);
        require(a.back() == 2);
        
        a.push_front(3);
        require(a[0] == 3);
        require(a[1] == 1);
        require(a[2] == 2);
        
        a.clear();
        require(a.is_empty());
        co_return;
    };
    
    
} // namespace wry
