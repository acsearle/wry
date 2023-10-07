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
    
    struct with_capacity_in_bytes_t {
        explicit with_capacity_in_bytes_t() = default;
    };
    
    inline constexpr with_capacity_t with_capacity{};
    inline constexpr with_capacity_in_bytes_t with_capacity_in_bytes{};

} // namespace wry

#endif /* with_capacity_hpp */
