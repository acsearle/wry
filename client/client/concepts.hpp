//
//  concepts.hpp
//  client
//
//  Created by Antony Searle on 27/7/2024.
//

#ifndef concepts_hpp
#define concepts_hpp

#include <concepts>

#include "type_traits.hpp"

namespace wry {
    
    template<typename From, typename To>
    concept PointerConvertibleTo = std::is_convertible_v<std::remove_cv_t<From>*, To*>;
    
    template<typename T>
    concept Relocatable = is_relocatable_v<T>;

} // namespace wry

#endif /* concepts_hpp */
