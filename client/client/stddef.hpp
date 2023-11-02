//
//  stddef.hpp
//  client
//
//  Created by Antony Searle on 2/10/2023.
//

#ifndef stddef_hpp
#define stddef_hpp

#include <cstddef>

namespace rust {
    
    using isize = std::ptrdiff_t;
    using usize = std::size_t;
    struct unit {};
    
} // namespace rust

namespace wry {
    
    using std::size_t;
    using std::ptrdiff_t;

    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    using byte = unsigned char;
            
} // namespace wry

#endif /* stddef_hpp */
