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
    
    // forward declare namespaces used for non-ADL fallback for types whose
    // implementations are not in their own namespaces, notably primitive types
    // and std types
    
    namespace sim {
        /* ... */
    } // namespace sim
    
    namespace adl {
        
        namespace _hidden {
            
            // customization point objects / Niebloids
            
            struct _swap {
                template<typename T>
                void operator()(T& a, T& b) const {
                    using namespace std;
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
            
        } // namespace _hidden
        
        constexpr _hidden::_swap swap;
        constexpr _hidden::_shade shade;
        constexpr _hidden::_trace trace;
        
    } // namespace adl
    
} // namespace wry

#endif /* adl_hpp */
