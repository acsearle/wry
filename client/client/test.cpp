//
//  test.cpp
//  client
//
//  Created by Antony Searle on 25/7/2023.
//

#include <mach/mach_time.h>

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
                uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                bool pass = false;
                try {
                    // head->print_metadata("running", 0.0);
                    head->run();
                    pass = true;
                } catch (...) {
                }
                uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                head->print_metadata(pass ? ": pass" : " fail", (t1 - t0) * 1e-9);
                delete exchange(head, head->next);
            }
            printf("[all] : pass\n");
            // std::quick_exit(EXIT_SUCCESS);
        }
        
    } // namespace detail
    
    void run_tests() {
        detail::test_t::run_all();
    }
    
} // namespace wry
