//
//  test.hpp
//  client
//
//  Created by Antony Searle on 25/7/2023.
//

#ifndef test_hpp
#define test_hpp

#include <vector>

#include "assert.hpp"
#include "preprocessor.hpp"

namespace wry {
    
    namespace detail {
        
        struct test_t {
            
            struct base {
                base* next;
                std::vector<const char*> _metadata;
                explicit base(std::vector<const char*> metadata)
                : _metadata(std::move(metadata)) {}
                virtual ~base() = default;
                virtual void run() = 0;
                void print_metadata(const char* suffix, double tau) {
                    printf("[");
                    if (!_metadata.empty()) for (size_t i = 0;;) {
                        printf("%s", _metadata[i]);
                        if (++i == _metadata.size())
                            break;
                        printf(",");
                    }
                    printf("] %s (%g seconds)\n", suffix, tau);
                }
            };
            
            template<typename X>
            struct derived : base {
                X _x;
                template<typename Y> derived(std::vector<const char*> metadata, Y&& y)
                : base(std::move(metadata))
                , _x(std::forward<Y>(y)) {}
                virtual ~derived() override = default;
                virtual void run() override {
                    _x();
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
            
            static void run_all();
            
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
    
    void run_tests();
    
} // namespace wry

#define define_test(...) static ::wry::detail::test_t WRY_CONCATENATE_TOKENS(_wry_detail_test_, __LINE__) = ::wry::detail::test_metadata_t{__VA_ARGS__} % []()

#endif /* test_hpp */
