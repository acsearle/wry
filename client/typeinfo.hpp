//
//  typeinfo.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef typeinfo_hpp
#define typeinfo_hpp

#include <string_view>
#include <typeinfo>

namespace wry {
    
    template<typename T>
    constexpr std::string_view type_name() {
        std::string_view p = __PRETTY_FUNCTION__;
        return std::string_view(p.data() + 39, p.size() - 39 - 1);
    }
    
} // namespace wry

#endif /* typeinfo_hpp */
