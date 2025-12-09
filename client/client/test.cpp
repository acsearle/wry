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
        
        void test_t::base::print_metadata(const char* suffix, double tau) {
            printf("[");
            if (!_metadata.empty()) for (size_t i = 0;;) {
                printf("%s", _metadata[i]);
                if (++i == _metadata.size())
                    break;
                printf(",");
            }
            printf("] %s (%g seconds)\n", suffix, tau);
        }


        // Head of intrusive list of tests constructed at startup on the main
        // thread.
        constinit test_t::base* _head = nullptr;
        

        test_t::base*& test_t::get_head() {
            return _head;
        }
        
        wry::Task test_t::run_all() {
            base* head = exchange(get_head(), nullptr);
            Coroutine::Nursery nursery;
            while (head) {
                co_await nursery.fork([](base* head) -> wry::Task {
                    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                    co_await (head->run());
                    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                    head->print_metadata("", (t1 - t0) * 1e-9);
                    delete head;
                    co_return;
                } (exchange(head, head->next)));
            }
            co_await nursery.join();
            printf("[all] : unit tests complete\n");
        }
        
    } // namespace detail
    
    wry::Task run_tests() {
        return detail::test_t::run_all();
    }
    
} // namespace wry
