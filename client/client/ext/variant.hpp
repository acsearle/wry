//
//  variant.hpp
//  client
//
//  Created by Antony Searle on 20/10/2025.
//

#ifndef variant_hpp
#define variant_hpp

#include <variant>

namespace wry {

    // Scan for std::variant types

    inline void garbage_collected_scan(std::monostate) {
        // no-op
    }

    template<typename... Args>
    inline void garbage_collected_scan(std::variant<Args...> const& x) {
        std::visit([](auto const& y){ garbage_collected_scan(y); }, x);
    }

} // namespace wry

#endif /* variant_hpp */
