//
//  vector.hpp
//  client
//
//  Created by Antony Searle on 20/10/2025.
//

#ifndef vector_hpp
#define vector_hpp

#include <vector>

namespace wry {
    
    template<typename T, typename Allocator>
    void garbage_collected_scan(std::vector<T, Allocator> const& values) {
        for (T const& value : values)
            garbage_collected_scan(value);
    }

} // namespace wry

#endif /* vector_hpp */
