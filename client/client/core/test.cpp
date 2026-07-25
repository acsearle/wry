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
        
        static bool _matches_filter(const test_t::base* test, std::string_view filter) {
            if (filter.empty())
                return true;
            for (const char* meta : test->_metadata) {
                if (std::string_view(meta).find(filter) != std::string_view::npos)
                    return true;
            }
            return false;
        }

        wry::Task test_t::run_all(std::string_view filter) {
            // Serially, by design: each test gets the worker pool to
            // itself, its printed time is its own, and a test that
            // blocks a worker or saturates the queue cannot starve the
            // others.  (Tests that want internal parallelism fork their
            // own nurseries.)
            base* head = exchange(get_head(), nullptr);
            while (head) {
                base* test = exchange(head, head->next);
                if (!_matches_filter(test, filter)) {
                    delete test;
                    continue;
                }
                uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                co_await (test->run());
                uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                test->print_metadata("", (t1 - t0) * 1e-9);
                delete test;
            }
            printf("[all] : unit tests complete\n");
        }

    } // namespace detail

    wry::Task run_tests(std::string_view filter) {
        return detail::test_t::run_all(filter);
    }
    
} // namespace wry
