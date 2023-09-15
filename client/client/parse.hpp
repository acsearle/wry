//
//  parse.hpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#ifndef parse_hpp
#define parse_hpp

#include "charconv.hpp"
#include "match.hpp"
#include "string.hpp"

namespace wry {
    
    // a parser is a matcher with side effects
    
    auto parse(auto&& matcher, auto&& effect) {
        return [matcher=std::forward<decltype(matcher)>(matcher),
                effect=std::forward<decltype(effect)>(effect)](string_view& v) -> bool {
            string_view u = v;
            return matcher(v) && ((void) effect(u / v), true);
        };
    }
    
    auto parse_number(auto& x) {
        return [&x](string_view& v) -> bool {
            std::from_chars_result result = wry::from_chars((const char*) v.a._ptr, (const char*) v.b._ptr, x);
            const uchar* p = reinterpret_cast<const uchar*>(result.ptr);
            return (v.a._ptr != p) && ((void) (v.a._ptr = p), true);
        };
    }
        
    auto parse_number_relaxed(auto& x) {
        return match_and(match_spaces(),
                         match_optional(match_character('+')),
                         parse_number(x));
    }
    
    inline auto parse_identifier(string& value) {
        return parse(match_identifier(), [&value](string_view match) {
            value = match;
        });
    }
    
} // namespace wry
    
#endif /* parse_hpp */
