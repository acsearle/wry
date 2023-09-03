//
//  test.cpp
//  client
//
//  Created by Antony Searle on 25/7/2023.
//

#include "utility.hpp"
#include "test.hpp"

namespace wry {
    
    namespace detail {
        
        test_t::base*& test_t::get_head() {
            static base* head = nullptr;
            return head;
        }
        
        void test_t::run_all() {
            base* head = exchange(get_head(), nullptr);
            {
                // reverse intrusive list
                base* p = nullptr;
                while (head)
                    rotate_left(p, head, head->next);
                std::swap(head, p);
            }
            while (head) {
                head->run();
                delete exchange(head, head->next);
            }
        }
        
    } // namespace detail
    
    void run_tests() {
        detail::test_t::run_all();
    }
    
} // namespace wry
