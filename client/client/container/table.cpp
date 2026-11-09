//
//  table.cpp
//  client
//
//  Created by Antony Searle on 26/7/2023.
//

#include <set>
#include <iostream>
#include <random>

#include "hash.hpp"
#include "table.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("table")
    {

        {
            Table<int, int> t;
            require(t.empty());
            require(t.size() == 0);
            require(t.begin() == t.end());
        }
        
        {
            Table<int, int> t;

            int N = 1000;
            
            t._inner._invariant();

            for (int k = 0; k != N; ++k) {
                require(t.size() == k);
                auto v = k % 3;
                auto [p, f] = t.insert_or_assign(k, v);
                require((*p == std::pair<const int, int>(k, v)));
                require(f);
            }

            t._inner._invariant();
            
            [[maybe_unused]] auto x = t._inner.total_displacement();
            //printf("avg disp %g\n", x / (double) t.size());

            require(t.size() == N);

            for (int k = 0; k != N; ++k) {
                auto v = k % 3;
                auto p = t.find(k);
                require(p != t.end());
                require((*p == std::pair<const int, int>(k, v)));
                require(t.size() == N);
               
                /*
                printf("{%llu, {%d, %d}} [%zd]/%llu\n",
                       p._pointer->_hash, k, v,
                       p._pointer - p._context->_begin,
                       p._context->size());*/
                
            }
            
            t._inner._invariant();
            
            for (int k = N; k != 2 * N; ++k) {
                auto p = t.find(k);
                require(p == t.end());
                require(t.size() == N);
            }
            
            t._inner._invariant();

            
            for (int k = 0; k != N; ++k) {
                
                t._inner._invariant();

                require(t.size() == N - k);
                auto n = t.erase(k);
                require(n == 1);
            }
            
            t._inner._invariant();
            
            require(t.size() == 0);
            require(t.empty());
            
        }
        
        {
            Table<int, int> t;
            int N = 1000;
            // two steps forward, one step back
            for (int j = 0; j != N; ++j) {
                int k;
                k = j * 2;
                t.insert_or_assign(k, k % 3);
                ++k;
                t.insert_or_assign(k, k % 3);
                k = j;
                auto n = t.erase(k);
                require(n == 1);
            }
            require(t.size() == N);
            for (int k = N; k != 2*N; ++k) {
                auto n = t.erase(k);
                require(n == 1);
            }
            require(t.size() == 0);

        }
        
        {
            Table<int, int> t;
            int N = 1000;
            for (int j = 0; j != N; ++j) {
                int k = j >> 1;
                auto [p, f] = t.insert_or_assign(k, j);
                require(f != (bool) (j & 1)); // insert when even, assign when odd
            }
            require(t.size() == N/2);
            int n = 0;
            for (auto [k, v] : t) {
                require(k == (v >> 1));
                require(v & 1); // we did overwrite
                ++n;
            }
            require(n == t.size());
            t.clear();
            require(t.empty());
        }
        
        {
            Table<int, int> t;
            int N = 1000;
            for (int j = 0; j != N; ++j) {
                int k = j >> 1;
                auto [p, f] = t.insert(std::pair<const int, int>(k, j));
                require(f != (bool) (j & 1)); // insert when even, nothing when odd
            }
            require(t.size() == N/2);
            int n = 0;
            for (auto [k, v] : t) {
                require(k == (v >> 1));
                require(!(v & 1)); // we did not overwite
                ++n;
            }
            require(n == t.size());
            t.clear();
            require(t.empty());
        }
        
        co_return;
        
    };
    
} // namespace wry

/*
 int main(int, char**) {
 test::test_table();
 return 0;
 }
 */
