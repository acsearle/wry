//
//  adl.hpp
//  client
//
//  Created by Antony Searle on 1/9/2024.
//

#ifndef adl_hpp
#define adl_hpp

#include <utility>

namespace wry {

namespace sim {
} // namespace sim

namespace adl {

namespace _adl {

struct _swap {
    template<typename T>
    void operator()(T& a, T& b) const {
        using std::swap;
        swap(a, b);
    }
}; // struct _swap

struct _shade {
    template<typename T>
    void operator()(const T& x) const {
        // namespaces for non-ADL trace implementations
        using namespace wry::sim;
        // resolve via ADL (mostly)
        shade(x);
    }
}; // struct _shade

struct _trace {
    template<typename T>
    void operator()(const T& x) const {
        // namespaces for non-ADL trace implementations
        using namespace wry::sim;
        // resolve via ADL (mostly)
        trace(x);
    }
}; // struct _trace

} // namespace _adl

constexpr _adl::_swap swap;
constexpr _adl::_shade shade;
constexpr _adl::_trace trace;


} // namespace adl
} // namespace wry

#endif /* adl_hpp */
