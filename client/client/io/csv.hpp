//
//  csv.hpp
//  client
//
//  Created by Antony Searle on 2/10/2023.
//

#ifndef csv_hpp
#define csv_hpp

#include <stdexcept>

#include "deserialize.hpp"
#include "parse.hpp"

namespace wry::csv::de {
        
    using rust::usize;
    using rust::option::Option;
    using rust::option::None;
    using rust::option::Some;

    inline auto parse_field(ContiguousDeque<char>& x) {
        return [&x](auto& v) -> bool {
            auto u(v);
            if (u.empty())
                return true;
            auto ch = u.front();
            if (ch != '\"') {

                // TODO: Permit whitespaces before escaped field?

                // is a non-escaped field
                for (;;) {
                    switch (ch) {
                        case '\n': // ends a line (extension)
                        case '\r': // ends a line
                        case ',':  // ends a field
                            v.reset(u);
                            return true;
                        case '\"': // dquotes prohibited in unescaped fields
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
                    if (ch == '\"') {
                        // is the end of the string or the first character of an
                        // escaped double-quote
                        u.pop_front();
                        if (u.empty() || ((ch = u.front()) != '\"')) {
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
    

    // Serde may be a bad fit for CSV, it simply isn't general enough a format

    
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
        ContiguousView<const char>& v;

        auto deserialize_string(auto&& visitor) {
            String x{};
            // string_from_file now validates at the boundary, so `v` is
            // valid UTF-8 by construction when obtained from String /
            // StringView.  This post-parse utf8::isvalid only earns its
            // keep against callers that built `v` from a raw byte source
            // and skipped validation.  Likely droppable once we audit the
            // upstream paths.
            if (!parse_field(x.chars)(v) || !utf8::isvalid(x.chars))
                throw std::invalid_argument("CSV: invalid UTF-8 in field");
            return std::forward<decltype(visitor)>(visitor).visit_string(std::move(x));
        }

#define X(T)\
        auto deserialize_##T(auto&& visitor) {\
            T x{};\
            if (!parse_number(x)(v))\
                throw std::invalid_argument("CSV: invalid number");\
            return std::forward<decltype(visitor)>(visitor).visit_##T(std::move(x));\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        
#undef X
        
        
    };
    
    struct RowDeserializer {
        ContiguousView<const char>& v;
        auto deserialize_seq(auto&& visitor) {
            FieldDeserializer d{v};
            return std::forward<decltype(visitor)>(visitor).visit_seq(DelimiterSeparated(&d, match_character(',')));
        }
    };

    struct Deserializer {
        ContiguousView<const char> v;
        auto deserialize_seq(auto&& visitor) {
            RowDeserializer d{v};
            return std::forward<decltype(visitor)>(visitor).visit_seq(DelimiterSeparated(&d, match_newline()));
        }
    };
    
} // namespace wry::csv::se

#endif /* csv_hpp */
