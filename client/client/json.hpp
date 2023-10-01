//
//  json.hpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#ifndef json_hpp
#define json_hpp

#include <expected>
#include <optional>

#include "cctype.hpp"
#include "charconv.hpp"
#include "parse.hpp"
#include "serialize.hpp"
#include "string.hpp"
#include "table.hpp"
#include "utility.hpp"
#include "vector.hpp"


namespace wry {
    
    inline std::expected<int, int> foo() {
        return std::expected<int, int>{};
    }
    
    bool json_parse_number(array_view<const char>& a, auto& z) {
        const char* first = a.begin();
        auto [ptr, ec] = from_chars(first, a.end(), z);
        a._begin += (ptr - first);
        return ptr == first;
    }

    
    
    inline constexpr bool is_json_whitespace(uint ch) {
        return (ch == '\t') || (ch == '\n') || (ch == '\r') || (ch == ' ');
    }
    
    inline constexpr bool is_json_stringbody(uint ch) {
        return ((ch != '\"')
         && (ch != '\\')
         && !(isuchar(ch)
              && iscntrl(ch)));
    }
    
    // json matchers
    
    inline auto match_json5_comment() {
        return match_and(match_string("//"), match_line());
    }
    
    inline auto match_json_whitespace() {
        return match_star(match_from("\t\n\r "));
    }
    
    inline auto match_json5_whitespace() {
        return match_star(match_or(match_from("\t\n\r "),
                                   match_json5_comment()));
    }
    
    inline auto match_json_number() {
        return match_and(match_optional(match_character('-')),
                         match_or(match_character('0'),
                                  match_and(match_nonzero_digit(),
                                            match_digit())),
                         match_optional(match_fractional_digits(),
                                        match_exponent()));
    }
        
    inline auto match_json_string_character() {
        return match_predicate([](int character) {
            return ((character != '\"')
                    && (character != '\\')
                    && !(isuchar(character)
                         && iscntrl(character)));
        });
    }
    
    inline auto match_json_string_escape_sequence() {
        return match_and(match_character('\\'),
                         match_or(match_from("\"\\/bfnrt"),
                                  match_and(match_character('u'),
                                            match_count(match_xdigit(), 4))));
    }
    
    inline auto match_json_string() {
        return match_and(match_character('\"'),
                         match_star(match_or(match_json_string_character(),
                                             match_json_string_escape_sequence())),
                         match_character('\"'));
    }

    inline auto match_json_name() {
        return match_and(match_json_whitespace(),
                         match_json_string(),
                         match_json_whitespace());
    }

    inline constexpr bool (*match_json_object())(string_view&);
    inline constexpr bool (*match_json_array())(string_view&);
    inline constexpr bool (*match_json_value())(string_view&);

    inline constexpr bool (*match_json_value())(string_view&) {
        return [](string_view& v) -> bool {
            return match_and(match_json_whitespace(),
                             match_or(match_json_string(),
                                      match_json_number(),
                                      match_json_object(),
                                      match_json_array(),
                                      match_string("true"),
                                      match_string("false"),
                                      match_string("null")),
                             match_json_whitespace())(v);
        };
    }
    
    inline constexpr bool (*match_json_array())(string_view&) {
        return [](string_view& v) -> bool {
            return match_and(match_character('['),
                             match_delimited(match_json_value(),
                                             match_character(',')),
                             match_json_whitespace(),
                             match_character(']'))(v);
        };
    }
    
    inline auto match_json_name_value_pair() {
        return match_and(match_json_name(),
                         match_character(':'),
                         match_json_value());
                         
    }
    
    inline constexpr bool (*match_json_object())(string_view&) {
        return [](string_view& v) -> bool {
            return match_and(match_character('{'),
                             match_delimited(match_json_name(),
                                             match_character(',')),
                             match_json_whitespace(),
                             match_character('}'))(v);
        };
    }
    
   
    
    
    
    
    
    struct _json_value;
    
    struct json {
        
        _json_value* _ptr;
        
        json() : _ptr(nullptr) {}
        explicit json(_json_value* p) : _ptr(p) {}
        json(json const&);
        json(json&& x) : _ptr(std::exchange(x._ptr, nullptr)) {}
        ~json();
        json& operator=(json const&);
        json& operator=(json&& x) { json tmp(std::move(x)); std::swap(_ptr, tmp._ptr); return *this; }
        
        static json from(string_view&);
        static json from(string_view&&);
        static json from_file(FILE*);
        
        size_t size() const;
        
        json const& operator[](size_t i) const;
        json const& operator[](string_view s) const;
        
        string_view as_string() const;
        bool as_bool() const;
        double as_number() const;
        table<string, json> const& as_object() const;
        array<json> const& as_array() const;
        
        long as_long() const;
        
        void swap(json& other) {
            std::swap(_ptr, other._ptr);
        }
        
    }; // json
    
    inline void swap(json& x, json& y) {
        x.swap(y);
    }
    
    std::ostream& operator<<(std::ostream&, json const&);
    
} // namespace wry

namespace wry {
    
    struct jso {
        enum {
            JSO_NULL,
            JSO_BOOLEAN,
            JSO_INTEGER,
            JSO_REAL,
            JSO_STRING,
            JSO_ARRAY,
            JSO_OBJECT,
        } _discriminant;
        union {
            double _double;
            bool _bool;
            long _int;
            string _string;
            array<jso> _array; // 32
            table<string, jso> _table; // 48 ?
        };
    };
    
} // namespace wry

namespace wry {
    
    // json validator
    
    
    
};

namespace wry {
    
    struct json_serializer {
        string s;
    };
    
    inline bool serialize(int x, json_serializer& s) {
        s.s._bytes.may_write_back(32);
        std::to_chars_result r = std::to_chars(s.s._bytes._end, s.s._bytes._allocation_end, x);
        auto n = r.ptr - s.s._bytes._end;
        s.s._bytes.did_write_back(n);
        return n;
    }

    inline bool serialize(double x, json_serializer& s) {
        s.s._bytes.may_write_back(32);
        std::to_chars_result r = std::to_chars(s.s._bytes._end, s.s._bytes._allocation_end, x);
        auto n = r.ptr - s.s._bytes._end;
        s.s._bytes.did_write_back(n);
        return n;
    }
   
    inline bool serialize(const string& x, json_serializer& s) {
        s.s.push_back('\"');
        for (u32 ch : x) {
            switch (ch) {
                case '\"':
                    s.s.append("\\\"");
                    break;
                case '\\':
                    s.s.append("\\\\");
                    break;
                case '\b':
                    s.s.append("\\b");
                    break;
                case '\f':
                    s.s.append("\\f");
                    break;
                case '\n':
                    s.s.append("\\f");
                    break;
                case '\r':
                    s.s.append("\\f");
                    break;
                case '\t':
                    s.s.append("\\f");
                    break;
                default:
                    if (isascii(ch) && iscntrl(ch)) {
                        s.s._bytes.may_write_back(16);
                        int count = snprintf(s.s._bytes._end, 7, "\\u%04x", ch);
                        if (count < 0)
                            return false;
                        s.s._bytes.did_write_back(count);
                    } else {
                        s.s.push_back(ch);
                    }
            }
        }
        s.s.push_back('\"');
        return false;
    }

    bool serialize_iterable(auto first, auto last, json_serializer& s) {
        s.s.append("[ ");
        for (;;) {
            if (first == last)
                break;
            serialize(*first, s);
            ++first;
            if (first == last)
                break;
            s.s.append(", ");
        }
        s.s.append(" ]");
        return true;
    }
    
    
    template<typename T, typename U>
    bool serialize(const std::pair<T, U>& x, json_serializer& s) {
        s.s.append("[ ");
        serialize(x.first, s);
        s.s.append(", ");
        serialize(x.second, s);
        s.s.append(" ]");
    }
    
    template<typename... T>
    bool serialize(const std::tuple<T...>& x, json_serializer& s) {
        s.s.append("[ ");
        std::apply([&s](const auto&... y) {
            size_t n = sizeof...(y);
            ((serialize(y, s), s.s.append(--n ? ", " : " ]")), ...);
        }, x);
        return true;
    }
    
    // json can serialize structs and dictionaries as objects but not
    // general maps with nonstring keys
    //
    // should we thus serialize maps to [..., [k, v], ...] ?
    //
    // for hash tables, we should sanitize the ordering too

    struct json_deserializer {
        
        array_view<char> v;
        string buffer;
        
        auto deserialize_f64(auto&& visitor) {
            f64 x = {};
            wry::from_chars(v.begin(), v.end(), x);
            return visitor.visit_f64(x);
        }
        
        auto deserialize_i64(auto&& visitor) {
            i64 x = {};
            std::from_chars(v.begin(), v.end(), x);
            return visitor.visit_i64(x);
        }
        
        auto deserialize_string(auto&& visitor) {
            
        }
        
        auto deserialize_sequence();
        
        auto deserialize_map();
        
        
    };
            
    
};

#endif /* json_hpp */
