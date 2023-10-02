//
//  json.hpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#ifndef json_hpp
#define json_hpp

#include "match.hpp"
#include "parse.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "table.hpp"

namespace wry {
    
    namespace json {
        
        // predicates
        
        inline constexpr bool is_json_whitespace(uint ch) {
            return (ch == '\t') || (ch == '\n') || (ch == '\r') || (ch == ' ');
        }
        
        // matchers
        
        inline auto match_json_whitespace() {
            return match_star(match_from("\t\n\r "));
        }
        
        // parsers
        
        inline auto parse_json_boolean(auto& x) {
            return [&x](string_view& v) -> bool {
                if (match_string("false")(v)) {
                    x = false;
                    return true;
                } else if (match_string("true")(v)) {
                    x = true;
                    return true;
                } else {
                    return false;
                }
            };
        }
        
        inline auto parse_json_number(auto& x) {
            return [&x](string_view& v) -> bool {
                std::from_chars_result result = wry::from_chars(v.a._ptr, v.b._ptr, x);
                if (result.ptr == v.a._ptr)
                    return false;
                v.a._ptr = result.ptr;
                return true;
            };
        }
        
        inline auto parse_json_string(string& x) {
            return [&x](string_view& v) -> bool {
                string_view u(v);
                if (!u)
                    return false;
                auto ch = *u;
                if (ch != '\"')
                    return false;
                for (;;) {
                    ++u;
                    if (!u)
                        return false;
                    ch = *u;
                    if (ch == '\"') {
                        ++u;
                        v = u;
                        printf("done with %s\n", x.c_str());
                        return true;
                    }
                    printf("push_back %c (%d)\n", ch, ch);
                    x.push_back(ch);
                }
                
            };
        }
        
        inline auto match_json_array_begin() {
            return match_and(match_json_whitespace(),
                             match_character('['));
        }
        
        inline auto match_json_comma() {
            return match_and(match_json_whitespace(),
                             match_character(','));
        }
        
        inline auto match_json_array_end() {
            return match_and(match_json_whitespace(),
                             match_character(']'));
        }
        
        inline auto match_json_object_begin() {
            return match_and(match_json_whitespace(),
                             match_character('{'));
        }
        
        inline auto match_json_colon() {
            return match_and(match_json_whitespace(),
                             match_character(':'));
        }
        
        inline auto match_json_object_end() {
            return match_and(match_json_whitespace(),
                             match_character('}'));
        }
        
        struct value {
            
            std::variant<
                std::monostate,
                bool,
                uint64_t,
                int64_t,
                float64_t,
                string,
                array<value>,
                table<string, value>
            > inner;

            bool is_null() const { return std::holds_alternative<std::monostate>(inner); }
            bool is_boolean() const { return std::holds_alternative<bool>(inner); }
            bool is_string() const { return std::holds_alternative<string>(inner); }
            bool is_array() const { return std::holds_alternative<array<value>>(inner); }
            bool is_object() const { return std::holds_alternative<table<string, value>>(inner); }

            string& as_string() & {
                return std::get<string>(inner);
            }
            
            array<value>& as_array() & {
                return std::get<array<value>>(inner);
            }
            
            table<string, value>& as_object() & {
                return std::get<table<string, value>>(inner);
            }
            
            value& operator[](size_t i) & {
                return as_array()[i];
            }
            
            value& operator[](string_view j) & {
                return as_object()[j];
            }
            
        };
        
        // aliases
        
        struct error {
        };
        
        template<typename T>
        using expected = std::expected<T, error>;
        
        using unexpected = std::unexpected<error>;
        
        
        struct _value_visitor {
            
            using value_type = value;

            expected<value> visit_none() {
                return value{{std::monostate{}}};
            }

            expected<value> visit_bool(bool x) {
                return value{{x}};
            }

            expected<value> visit_string(string x) {
                assert(x.invariant());
                return value{{std::move(x)}};
            }

            expected<value> visit_float64_t(float64_t x) {
                return value{{x}};
            }

            expected<value> visit_int64_t(int64_t x) {
                return value{{x}};
            }

            expected<value> visit_uint64_t(uint64_t x) {
                return value{{x}};
            }
            
            expected<value> visit_seq(auto&& accessor) {
                array<value> y;
                for (;;) {
                    std::optional<value> x(accessor.template next_element<value>());
                    if (x) {
                        printf("got a seq element\n");
                        y.push_back(*(std::move(x)));
                    }
                    else
                        return value{{std::move(y)}};
                }
            }
            
            expected<value> visit_map(auto&& accessor) {
                table<string, value> y;
                for (;;) {
                    std::optional<std::pair<string, value>> x(accessor.template next_entry<string, value>());
                    if (x) {
                        auto [at, flag] = y.insert(*std::move(x));
                        if (!flag)
                            return unexpected(std::in_place);
                    } else {
                        return value{{std::move(y)}};
                    }
                }
            }

            
        };

        template<typename D>
        std::expected<value, typename std::decay_t<D>::error_type>
        deserialize(std::in_place_type_t<value>, D&& deserializer) {
            return std::forward<D>(deserializer).deserialize_any(_value_visitor{});
        }
        
        
    
     
                
        
        // serialization
        
        struct serializer {
            
        };
        
        struct deserializer {
            
            using error_type = error;
            
            string_view& v;
                        
            struct seq_accessor {
                
                using error_type = error_type;
                
                deserializer* _parent;
                bool _expect_delimiter = false;
                
                template<typename T>
                std::optional<T> next_element() {
                                        
                    auto& v = _parent->v;
                    
                    if (match_json_array_end()(v) || (_expect_delimiter && !match_json_comma()(v)))
                        return {};
                    expected<T> result = wry::deserialize<T>(*_parent);
                    if (!result)
                        return {};
                    _expect_delimiter = true;
                    return *std::move(result);
                }
                
            };
            
            seq_accessor make_seq_accessor() {
                return seq_accessor{this};
            };

            struct map_accessor {
                
                using error_type = error_type;
                
                deserializer* _parent;
                bool _expect_delimiter = false;
                
                template<typename K, typename T>
                std::optional<std::pair<K, T>> next_entry() {
                    auto& v = _parent->v;
                    if (match_json_object_end()(v) || (_expect_delimiter && match_json_comma()(v)))
                        return {};
                    using wry::deserialize;
                    expected<K> key = deserialize<K>(*_parent);
                    if (!key)
                        return {};
                    if (!match_json_colon()(v))
                        return {};
                    expected<T> value = deserialize<T>(*_parent);
                    if (!value)
                        return {};
                    _expect_delimiter = true;
                    return std::make_pair(*std::move(key), *std::move(value));
                }
                
            };
            
            map_accessor make_map_accessor() {
                return map_accessor{this};
            };

            template<typename V>
            expected<typename std::decay_t<V>::value_type>
            deserialize_any(V&& visitor) {
                
                match_json_whitespace()(v);
                
                if (match_string("null")(v))
                    return std::forward<V>(visitor).visit_none();
                    
                if (bool x = {}; parse_json_boolean(x)(v))
                    return std::forward<V>(visitor).visit_bool(x);
                    
                if (uint64_t x = {}; parse_json_number(x)(v))
                    return std::forward<V>(visitor).visit_uint64_t(x);
                    
                if (string x; parse_json_string(x)(v)) {
                    return std::forward<V>(visitor).visit_string(x);
                }
                    
                if (match_json_array_begin()(v))
                    return std::forward<V>(visitor).visit_seq(make_seq_accessor());
                
                if (match_json_object_begin()(v))
                    return std::forward<V>(visitor).visit_map(make_map_accessor());
                
                return unexpected(std::in_place);
                
            }
            
            template<typename V>
            expected<typename std::decay_t<V>::value_type>
            deserialize_bool(V&& visitor) {
                match_json_whitespace()(v);
                if (!v)
                    return unexpected(std::in_place);
                bool x;
                if (!parse_json_boolean(x)(v))
                    return unexpected(std::in_place);
                return std::forward<V>(visitor).visit_bool(x);
            }
            
            template<typename V> 
            expected<typename std::decay_t<V>::value_type>
            deserialize_int64_t(V&& visitor) {
                match_json_whitespace()(v);
                int64_t x;
                if (!parse_json_number(x)(v))
                    return unexpected(std::in_place);
                return std::forward<V>(visitor).visit_int64_t(x);
            }
            
            template<typename V>
            expected<typename std::decay_t<V>::value_type>
            deserialize_string(V&& visitor) {
                match_json_whitespace()(v);
                string x;
                if (!parse_json_string(x)(v))
                    return unexpected(std::in_place);
                return std::forward<V>(visitor).visit_string(x);
            }
            
            template<typename V>
            expected<typename std::decay_t<V>::value_type>
            deserialize_seq(V&& visitor) {
                if (!match_json_array_begin()(v))
                    return unexpected(std::in_place);
                return std::forward<V>(visitor).visit_seq(make_seq_accessor());
            }
            
        };
        
        template<typename T>
        expected<T> from_string(string s) {
            string_view v = s;
            return wry::deserialize<T>(deserializer{v});
        }
        
        template<typename T>
        expected<T> from_file(string_view name) {
            string s = string_from_file(name);
            string_view v = s;
            return wry::deserialize<T>(deserializer{v});
        }
        
    } // namespace json
    
} // namespace wry

#if 0

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

#endif

#endif /* json_hpp */
