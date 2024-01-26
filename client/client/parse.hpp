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
                effect=std::forward<decltype(effect)>(effect)](StringView& v) mutable -> bool {
            StringView u = v;
            return matcher(v) && ((void) effect(u / v), true);
        };
    }
    
    inline constexpr auto parse_until(auto&& many, auto&& once, auto&& action) {
        return [many=std::forward<decltype(many)>(many),
                once=std::forward<decltype(once)>(once),
                action=std::forward<decltype(action)>(action)](auto& v) mutable -> bool {
            auto w(v);
            for (auto u(v); ; ) {
                if (once(u)) {
                    action(w / v, v / u);
                    v.reset(u);
                    return true;
                }
                if (!many(u))
                    return false;
            }
        };
    }
    
    
    inline constexpr auto parse_number(auto& x) {
        return overloaded{
            [&x](StringView& v) -> bool {
                auto first = reinterpret_cast<const char*>(v.chars.begin());
                auto last = reinterpret_cast<const char*>(v.chars.end());
                std::from_chars_result result = wry::from_chars(first, last, x);
                if (result.ptr == first)
                    return false;
                v.chars._begin += (result.ptr - first);
                return true;
            },
            [&x](array_view<const char8_t>& v) -> bool {
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
        
    inline constexpr auto parse_number_relaxed(auto& x) {
        return match_and(match_spaces(),
                         match_optional(match_character('+')),
                         parse_number(x));
    }

    inline auto parse_identifier(String& value) {
        return parse(match_identifier(), 
                     [&value](StringView match) {
            value = match;
        });
    }
        
} // namespace wry
    
#endif /* parse_hpp */
