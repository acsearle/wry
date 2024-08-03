//
//  concepts.hpp
//  client
//
//  Created by Antony Searle on 27/7/2024.
//

#ifndef concepts_hpp
#define concepts_hpp

#include <concepts>

namespace wry {
    
    template<class From, class To>
    concept PointerConvertibleTo = std::is_convertible_v<From*, To*>;

} // namespace wry

#endif /* concepts_hpp */
