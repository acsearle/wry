//
//  with_capacity.hpp
//  client
//
//  Created by Antony Searle on 26/7/2023.
//

#ifndef with_capacity_hpp
#define with_capacity_hpp

namespace wry {
    
    struct with_capacity_t {
        explicit with_capacity_t() = default;
    };
    
    inline constexpr with_capacity_t with_capacity{};

} // namespace wry

#endif /* with_capacity_hpp */
