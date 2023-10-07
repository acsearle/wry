//
//  csv.hpp
//  client
//
//  Created by Antony Searle on 2/10/2023.
//

#ifndef csv_hpp
#define csv_hpp

#include "parse.hpp"
#include "Option.hpp"

namespace wry::csv::de {
    
    using rust::usize;
    using rust::option::Option;
    using rust::option::None;
    using rust::option::Some;

    inline auto parse_field(array<char8_t>& x) {
        return [&x](auto& v) -> bool {
            auto u(v);
            if (u.empty())
                return true;
            auto ch = u.front();
            if (ch != u8'\"') {
                
                // is a non-escaped field
                for (;;) {
                    switch (ch) {
                        case u8'\n': // ends a line (extension)
                        case u8'\r': // ends a line
                        case u8',':  // ends a field
                            v.reset(u);
                            return true;
                        case u8'\"': // dquotes prohibited in unescaped fields
                            return false;
                        default:     // anything else (extension: iscntrl)
                            break;
                    }
                    x.push_back(ch);
                    u.pop_front();
                    if (u.empty()) {
                        v.reset(u);
                        return true;
                    }
                    ch = u.front();
                }
                
            } else {
                
                // is an escaped field
                for (;;) {
                    u.pop_front();
                    if (u.empty()) {
                        // unterminated string
                        return false;
                    }
                    ch = u.front();
                    if (ch == u8'\"') {
                        // is the end of the string or the first character of an
                        // escaped double-quote
                        u.pop_front();
                        if (u.empty() || ((ch = u.front()) != u8'\"')) {
                            // is the end of the string
                            v.reset(u);
                            return true;
                        }
                        // is escaped double-quote, emit one double-quote
                    }
                    x.push_back(ch);
                }
                
            }
        };
    }
    

    
    template<typename Deserializer, typename Matcher>
    struct DelimiterSeparated {
        
        Deserializer* _deserializer;
        Matcher _matcher;
        Option<usize> _size_hint;
        bool first = true;
        
        DelimiterSeparated(Deserializer* p, Matcher&& m)
        : _deserializer(p)
        , _matcher(std::move(m)) {
        }
        
        
        template<typename T>
        Option<T> next_element() {
            auto& v = _deserializer->v;
            if (first)
                first = false;
            else if (!_matcher(v))
                return None();
            return Some(deserialize<T>(*_deserializer));
        };
        
        Option<usize> size_hint() const {
            return _size_hint;
        }
        
    };
    
    struct FieldDeserializer {
        array_view<const char8_t>& v;
        
        auto deserialize_string(auto&& visitor) {
            string x{};
            if (!parse_field(x.chars)(v) || !utf8::isvalid(x.chars))
                throw EINVAL;            
            return std::forward<decltype(visitor)>(visitor).visit_string(std::move(x));
        }
        
#define X(T)\
        auto deserialize_##T(auto&& visitor) {\
            T x{};\
            if (!parse_number(x)(v))\
                throw EINVAL;\
            return std::forward<decltype(visitor)>(visitor).visit_##T(std::move(x));\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        
#undef X
        
        
    };
    
    struct RowDeserializer {
        array_view<const char8_t>& v;
        auto deserialize_seq(auto&& visitor) {
            FieldDeserializer d{v};
            return std::forward<decltype(visitor)>(visitor).visit_seq(DelimiterSeparated(&d, match_character(u8',')));
        }
    };
    
    struct Deserializer {
        array_view<const char8_t> v;
        auto deserialize_seq(auto&& visitor) {
            RowDeserializer d{v};
            return std::forward<decltype(visitor)>(visitor).visit_seq(DelimiterSeparated(&d, match_newline()));
        }
    };
    
} // namespace wry::csv::se

#endif /* csv_hpp */
