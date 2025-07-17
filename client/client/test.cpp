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

        constinit static test_t::base* _head = nullptr;

        test_t::base*& test_t::get_head() {
            return _head;
        }
        
        void test_t::run_all() {
            base* head = exchange(get_head(), nullptr);
            {
                // reverse intrusive list
                base* p = nullptr;
                while (head)
                    rotate_args_left(p, head, head->next);
                std::swap(head, p);
            }
            while (head) {
                head->run();
                delete exchange(head, head->next);
            }
            printf("[all] : pass\n");
            std::quick_exit(EXIT_SUCCESS);
        }
        
    } // namespace detail
    
    void run_tests() {
        detail::test_t::run_all();
    }
    
} // namespace wry
