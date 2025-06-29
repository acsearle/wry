//
//  adl.hpp
//  client
//
//  Created by Antony Searle on 1/9/2024.
//

#ifndef adl_hpp
#define adl_hpp

#include <utility>

    
// The intent of this file is to provide access points of the form
//
//     adl::foo(x)
//
// which find the correct implementation of foo by ADL even when the calling
// context shadows those names.
//
// To provide customization points for types in closed namespaces (notably std)
// we also may explicitly import a namespace
    

// Forward declare namespaces

namespace wry {
    
    namespace sim {}
        
} // namespace wry

#define MAKE_CUSTOMIZATION_POINT_OBJECT(NAME, NAMESPACE)\
namespace adl {\
    namespace _detail {\
        struct _##NAME {\
            decltype(auto) operator()(auto&&... args) const {\
                using namespace NAMESPACE;\
                return NAME(std::forward<decltype(args)>(args)...);\
            }\
        };\
    }\
    constexpr _detail::_##NAME NAME;\
}

MAKE_CUSTOMIZATION_POINT_OBJECT(swap, ::std)
MAKE_CUSTOMIZATION_POINT_OBJECT(shade, ::wry::sim)
MAKE_CUSTOMIZATION_POINT_OBJECT(trace, ::wry::sim)
    
#endif /* adl_hpp */
