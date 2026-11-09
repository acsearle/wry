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

        void require_failed(const char* expression, const char* file, int line) {
            fprintf(stderr, "%s:%d: require(%s) failed\n", file, line, expression);
            fflush(stderr);
            std::abort();
        }

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

        static bool _is_exclusive(const test_t::base* test) {
            for (const char* meta : test->_metadata)
                if (std::string_view(meta) == test_exclusive)
                    return true;
            return false;
        }

        wry::Task test_t::run_all(std::string_view filter, bool shared) {
            // Serially, by design: each test gets the rest of the suite out
            // of its way, its printed time is its own, and a test that
            // saturates the queue cannot starve the others.  (Tests that
            // want internal parallelism fork their own nurseries.)  The
            // pool and the reactor are shared with the live game when there
            // is one, so a test that cannot share declares itself exclusive
            // and sits out a live run.
            base* head = take(get_head());
            size_t skipped = 0;
            while (head) {
                base* test = exchange(head, head->next);
                if (!_matches_filter(test, filter)) {
                    delete test;
                    continue;
                }
                if (shared && _is_exclusive(test)) {
                    test->print_metadata("skipped: exclusive, and the process is live", 0.0);
                    ++skipped;
                    delete test;
                    continue;
                }
                uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                co_await (test->run());
                uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                test->print_metadata("", (t1 - t0) * 1e-9);
                delete test;
            }
            if (skipped)
                printf("[all] : unit tests complete (%zu exclusive tests skipped; run them with --test-only)\n", skipped);
            else
                printf("[all] : unit tests complete\n");
        }

    } // namespace detail

    wry::Task run_tests(std::string_view filter, bool shared) {
        return detail::test_t::run_all(filter, shared);
    }
    
} // namespace wry
