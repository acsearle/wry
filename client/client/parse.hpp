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
                effect=std::forward<decltype(effect)>(effect)](string_view& v) mutable -> bool {
            string_view u = v;
            return matcher(v) && ((void) effect(u / v), true);
        };
    }
    
    auto parse_number(auto& x) {
        return overloaded{
            [&x](string_view& v) -> bool {
                //printf("parse_number(sv) %.10s\n", (const char*) v.begin().base);
                auto first = reinterpret_cast<const char*>(v.chars.begin());
                auto last = reinterpret_cast<const char*>(v.chars.end());
                std::from_chars_result result = wry::from_chars(first, last, x);
                if (result.ptr == first)
                    return false;
                //printf("%g\n", (double) x);
                v.chars._begin += (result.ptr - first);
                return true;
            },
            [&x](array_view<const char8_t>& v) -> bool {
                //printf("parse_number(ch*) %.10s\n", (const char*) v.begin());
                auto first = reinterpret_cast<const char*>(v.begin());
                auto last = reinterpret_cast<const char*>(v.end());
                std::from_chars_result result = wry::from_chars(first, last, x);
                if (result.ptr == first)
                    return false;
                v._begin += (result.ptr - first);
                return true;
            }
        };
    }
        
    auto parse_number_relaxed(auto& x) {
        return match_and(match_spaces(),
                         match_optional(match_character('+')),
                         parse_number(x));
    }

    inline auto parse_graphs(string& value) {
        return parse(match_graphs(), 
                     [&value](string_view match) {
            value = match;
        });
    }

    inline auto parse_identifier(string& value) {
        return parse(match_identifier(), [&value](string_view match) {
            value = match;
        });
    }
    
    
} // namespace wry
    
#endif /* parse_hpp */
