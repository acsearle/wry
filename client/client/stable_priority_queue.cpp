//
//  stable_priority_queue.cpp
//  client
//
//  Created by Antony Searle on 5/11/2023.
//

#include <random>

#include "debug.hpp"
#include "stable_priority_queue.hpp"
#include "test.hpp"

namespace wry {
    
    struct int_compare {
        bool operator()(int a, int b) const {
            
            //if ((a >> 1) > (b >> 1)) {
            //    DUMP(a);
            //    DUMP(b);
            //}
            
            return (a >> 5) < (b >> 5);
        }
    };
    
    define_test("stable-priority-queue") {
        
       
        
        StablePriorityQueue<int, int_compare> q;
        
        int N = 1000;
        
        std::random_device rd;
        std::default_random_engine rne(rd());
        std::uniform_int_distribution<> uid; // 0 .. INT_MAX
        
        for (int i = N; i != 0; --i) {
            q.insert(i);
        }
        
        /*
        for (int i = 0; i != q._capacity; ++i) {
            printf("%d : %zd\n", i, q._sizes[i]);
        }
            
        for (int i = 0; i != 1 << q._capacity; ++i) {
            printf("%d : %d\n", i, q._elements[i]);
        }
         */
        
        for (int i = 0; i != q._capacity; ++i) {
            printf("_sizes[%d] = %zd\n", i, q._sizes[i]);
            int* last = q._elements + (2 << i);
            int* first = last - q._sizes[i];
            while (first != last) {
                printf("\t%d\n", *first++);
            }
        }
        
        for (int i = 0; i != 1000; ++i) {
            int j = q.stable_extract_min();
            DUMP(j);
        }

        
    };
}
