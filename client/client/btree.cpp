//
//  btree.cpp
//  wry
//
//  Created by Antony Searle on 16/1/2023.
//

#include <random>
#include <functional>
#include <vector>

#include "btree.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("btree") {

        int N = 1000;

        std::random_device rd;
        std::default_random_engine rne(rd());
        std::uniform_int_distribution<> uid; // 0 .. INT_MAX
        
        wry::btree_map<int, int> b;
        std::vector<std::pair<int, int>> v;
        std::vector<std::pair<int, int>> u;

        b._inner._assert_invariant();
        
        for (int i = 0; i != N; ++i) {
            int j = uid(rne);
            int k = uid(rne);
            v.emplace_back(j, k);
            b.insert_or_assign({j, k});
        }
        
        b._inner._assert_invariant();
        
        {
            // generate some absent entries too
            for (int i = 0; i != N; ++i) {
                int j = uid(rne);
                int k = uid(rne);
                u.emplace_back(j, k);
            }
            std::sort(v.begin(), v.end(), b._inner._comparator);
            std::sort(u.begin(), u.end(), b._inner._comparator);
            std::vector<std::pair<int, int>> w;
            w.reserve(N);
            std::set_difference(u.begin(), u.end(),
                                v.begin(), v.end(),
                                std::back_inserter(w),
                                b._inner._comparator);
            u.swap(w);
            assert(!u.empty());
            // v now contains {k, v} in b
            // u now contains {k[, v]} not in b
            // randomize order
            std::shuffle(v.begin(), v.end(), rne);
            std::shuffle(u.begin(), u.end(), rne);
        }
        
        
        // pointer find
        for (auto j : v) {
            printf("find %d -> %d\n", j.first, j.second);
            auto p = b._inner.pfind(j.first);
            assert(p && (*p == j));
            printf("    found {%d, %d}\n", p->first, p->second);
        }
        
        // pointer find absent
        for (auto j : u) {
            auto p = b._inner.pfind(j.first);
            assert(!p);
        }

        // find
        for (auto j : v) {
            auto p = b.find(j.first);
            assert((p != b.end()) && (*p == j));
        }

        // find absent
        for (auto j : u) {
            auto p = b.find(j.first);
            assert((p == b.end()));
        }
        
        // find hinted
        {
            std::vector<decltype(b)::iterator> w;
            for (auto it = b.begin(); it != b.end(); ++it) {
                w.push_back(it);
            }
            w.push_back(b.end());
            std::shuffle(w.begin(), w.end(), rne);
            for (std::size_t i = 0; i != v.size(); ++i) {
                auto hint = w[i];
                auto j = v[i];
                auto p = b.find(j.first, hint);
                assert((p != b.end()) && (*p == j));
            }
            std::shuffle(w.begin(), w.end(), rne);
            for (std::size_t i = 0; i != u.size(); ++i) {
                auto hint = w[i];
                auto j = u[i];
                auto p = b.find(j.first, hint);
                assert(p == b.end());
            }
        }
        
        b._inner._assert_invariant();
        
        for (std::size_t i = 0; i != u.size(); ++i) {
            b.erase(v[i].first);
            b.insert_or_assign({u[i].first, u[i].second});
        }
        
        assert(b.size() == u.size());

        b._inner._assert_invariant();
        
        for (auto j : v) {
            auto p = b.find(j.first);
            assert(p == b.end());
        }

        for (auto j : u) {
            auto p = b.find(j.first);
            assert((p != b.end()) && (*p == j));
        }
        
        b.clear();
        
        assert(b.size() == 0);
        assert(b.empty());

    };
    
} // namespace wry
