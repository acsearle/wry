//
//  test.hpp
//  client
//
//  Created by Antony Searle on 25/7/2023.
//

#ifndef test_hpp
#define test_hpp

#include <string_view>
#include <vector>

#include "assert.hpp"
#include "preprocessor.hpp"
#include "coroutine.hpp"

namespace wry {
    
    namespace detail {
        
        using coroutine::Task;
        
        struct test_t {
            
            struct base {
                base* next;
                std::vector<const char*> _metadata;
                explicit base(std::vector<const char*> metadata)
                : _metadata(std::move(metadata)) {}
                virtual ~base() = default;
                virtual Task run() = 0;
                void print_metadata(const char* suffix, double tau);
            };
            
            template<typename X>
            struct derived : base {
                X _x;
                template<typename Y> derived(std::vector<const char*> metadata, Y&& y)
                : base(std::move(metadata))
                , _x(std::forward<Y>(y)) {}
                virtual ~derived() override = default;
                virtual Task run() override {
                    return _x();
                }
            };
            
            static base*& get_head();

            template<typename X>
            test_t(std::vector<const char*> s, X&& x) {
                base*& head = get_head();
                base* p = new derived<std::decay_t<X>>(std::move(s), std::forward<X>(x));
                p->next = head;
                head = p;
            }

            static Task run_all(std::string_view filter, bool shared);
            
        };
        
        struct test_metadata_t {
            
            std::vector<const char*> _cstr_literals;
            
            test_metadata_t(std::initializer_list<const char*> ilist)
            : _cstr_literals(ilist)
            {
            }
            
            template<typename X>
            test_t operator%(X&& x) {
                return test_t(std::move(_cstr_literals), std::forward<X>(x));
            }
            
        };
        
    }
    
    using coroutine::Task;

    // A metadata tag for a test that needs the reactor and the worker pool
    // to itself: it reads a process-wide count, or asserts that something
    // happened promptly.  Such a test runs only when the suite is the
    // process's whole business (--test-only); a live process, which runs
    // the suite beside the game, skips it and says so.  Everything else in
    // the suite is written to share (io/test_support.hpp).
    inline constexpr const char* test_exclusive = "exclusive";

    // Runs every registered test whose metadata contains `filter` as a
    // substring (in any of its strings).  Empty filter runs all tests.
    // `shared` says the process has other work (a game, a live control
    // socket) on the same pool and reactor, so the exclusive tests are
    // skipped.
    Task run_tests(std::string_view filter = {}, bool shared = false);
    
} // namespace wry

// The check for test bodies.  Unlike assert it is evaluated in every build
// (the Release build runs the suite too, and an assert there is a check that
// silently isn't made, or worse, a side effect that silently isn't done).  On
// failure it prints the expression and location, then aborts.  `assert` is
// banned inside define_test bodies; tools/lint_test_asserts.py enforces that
// at build time.  An expression, so it composes like assert (e.g. in a comma
// fold); variadic so template commas need no extra parentheses.
namespace wry::detail {
    [[noreturn]] void require_failed(const char* expression, const char* file, int line);
}
#define require(...) \
    ((__VA_ARGS__) ? (void)0 : ::wry::detail::require_failed(#__VA_ARGS__, __FILE__, __LINE__))

#define define_test(...) static ::wry::detail::test_t WRY_CONCATENATE_TOKENS(_wry_detail_test_, __LINE__) = ::wry::detail::test_metadata_t{__VA_ARGS__} % []() -> wry::Task

#endif /* test_hpp */
