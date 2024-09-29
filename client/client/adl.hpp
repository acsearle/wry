//
//  adl.hpp
//  client
//
//  Created by Antony Searle on 1/9/2024.
//

#ifndef adl_hpp
#define adl_hpp

#include <utility>

namespace wry::sim { }

namespace wry::adl::_adl {

struct _swap {
    template<typename T>
    void operator()(T& a, T& b) const {
        using std::swap;
        swap(a, b);
    }
};

struct _shade {
    template<typename T>
    void operator()(const T& x) const {
        using namespace wry::sim;
        shade(x);
    }
};

struct _trace {
    template<typename T>
    void operator()(const T& x) const {
        using namespace wry::sim;
        trace(x);
    }
};

}


namespace wry::adl {

constexpr _adl::_swap swap;
constexpr _adl::_shade shade;
constexpr _adl::_trace trace;

}

#endif /* adl_hpp */
