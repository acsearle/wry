//
//  json.hpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#ifndef json_hpp
#define json_hpp

#include "base36.hpp"
#include "filesystem.hpp"
#include "match.hpp"
#include "parse.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "table.hpp"
#include "cstring.hpp"

#include "Option.hpp"
#include "stddef.hpp"

#include "value.hpp"

namespace wry::json {
    
    // JSON
    //
    // https://datatracker.ietf.org/doc/html/rfc8259
    //
    // UTF-8 coding for files is mandatory, but JSON uses UTF-16 surrogate
    // pairs in its escape sequence syntax
    
    // TODO:
    // - Consider simdjson
    // - Consider StringViews into allocation, to prevent individual
    //   allocations for numbers and strings of nonescaped characters
    //   - can it live forever as Atoms?
    // - Are we double-handling whitespace?
    
    
    // predicates
    
    inline constexpr bool is_json_whitespace(auto ch) {
        return ((ch == u8'\t') ||
                (ch == u8'\n') ||
                (ch == u8'\r') ||
                (ch == u8' ' ));
    }
    
    // matchers
    
    inline auto match_json_whitespace() {
        return match_star(match_from("\t\n\r "));
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
    
    inline auto match_json_true() {
        return match_string(u8"true");
    }
    
    inline auto match_json_false() {
        return match_string(u8"false");
    }
    
    inline auto match_json_null() {
        return match_string(u8"null");
    }
    
    inline auto match_json_number() {
        return match_and(match_optional(match_character('-')),
                         match_or(match_character('0'),
                                  match_plus(match_digit())),
                         match_optional(match_fractional_digits()),
                         match_optional(match_exponent()));
    }
    
    // parse json into basic types
    
    inline auto parse_json_boolean(auto& x) {
        return [&x](auto& v) -> bool {
            if (match_json_false()(v)) {
                x = false;
                return true;
            } else if (match_json_true()(v)) {
                x = true;
                return true;
            } else {
                return false;
            }
        };
    }
    
    
    // std::from_chars with base=10 is appropriate for JSON numbers
    
    // JSON strings are the most interesting part of the format.  We only
    // need to support UTF-8-encoded JSON, but we must still validate the
    // untrusted input.  When the JSON string contains no escape sequences,
    // we can simply memcpy it, or return a view into the raw input.  When
    // escape sequences are present, we no longer have a one-to-one mapping
    // and in the worst case, we have to construct a UTF-16 surrogate pair,
    // decode it, and then encode the resulting UTF-32 character into the
    // output.
    
    inline constexpr char _json_string_codeunit_class[256] = {
        
        3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3, // control characters
        3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,
        0,0,1,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, // double-quote
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 2,0,0,0, // reverse solidus
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,3, // delete
        
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        
        4,4,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, // invalid UTF-8 byte
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,4,4,4, 4,4,4,4, 4,4,4,4, // invalid UTF-8 byte
        
    };
    
    inline bool _json_string_parse_XXXX(ContiguousView<const char8_t>& v, char16_t& x) {
        auto first = reinterpret_cast<const char*>(v.begin());
        auto last = reinterpret_cast<const char*>(v.end());
        if (last - first < 4)
            return false;
        last = first + 4;
        std::from_chars_result result = from_chars(first, last, x, 16);
        if (result.ptr != last)
            return false;
        v._begin += 4;
        return true;
    }
    
    inline auto parse_json_string(String& x) {
        return [&x](ContiguousView<const char8_t>& v) -> bool {
            auto u(v);
            if (u.empty())
                return false;
            char8_t a = u.front();
            u.pop_front();
            if (a != u8'\"')
                return false;
            // -- fast path -----------------------------------------------
        expect_leading:
            if (u.empty())
                return false;
            a = u.front();
            u.pop_front();
            switch (_json_string_codeunit_class[a]) {
                case 0: // simple bytewise copy
                    x.chars.push_back(a);
                    goto expect_leading;
                    // -- -------------------------------------------------
                case 1: // unescaped double-quote ends string
                    v.reset(u);
                    x.chars.push_back(u8'\0');
                    x.chars.pop_back();
                    return true;
                case 2:
                    goto continue_escape_sequence;
                default:
                    // invalid JSON or UTF-8
                    return false;
            }
        continue_escape_sequence:
            if (u.empty())
                return false;
            a = u.front();
            u.pop_front();
            switch (a) {
                    // self-escaping sequences
                case u8'\"':
                case u8'\\':
                case u8'/':
                    x.chars.push_back(a);
                    goto expect_leading;
                case u8'b':
                    x.chars.push_back(u8'\b');
                    goto expect_leading;
                case u8'f':
                    x.chars.push_back(u8'\f');
                    goto expect_leading;
                case u8'n':
                    x.chars.push_back(u8'\n');
                    goto expect_leading;
                case u8'r':
                    x.chars.push_back(u8'\r');
                    goto expect_leading;
                case u8't':
                    x.chars.push_back(u8'\t');
                    goto expect_leading;
                case u8'u': {
                    char16_t y[2] = {};
                    if (!_json_string_parse_XXXX(u, y[0]))
                        return false;
                    // printf("Got \\u%0.4X\n", y[0]);
                    if (utf16::islowsurrogate(y[0]))
                        return false;
                    if (!utf16::ishighsurrogate(y[0])) {
                        x.chars.push_back(u8'\0');
                        x.chars.pop_back();
                        x.push_back(y[0]);
                        goto expect_leading;
                    }
                    if (u.empty() || (u.front() != u8'\\'))
                        return false;
                    u.pop_front();
                    if (u.empty() || (u.front() != u8'u'))
                        return false;
                    u.pop_front();
                    if (!_json_string_parse_XXXX(u, y[1]))
                        return false;
                    // printf("Got \\u%0.4X\n", y[1]);
                    if (!utf16::islowsurrogate(y[1]))
                        return false;
                    x.chars.push_back(u8'\0');
                    x.chars.pop_back();
                    x.push_back(utf16::decodesurrogatepair(y));
                    goto expect_leading;
                }
                default:
                    // invalid escape sequence
                    return false;
            }
            
        };
    }
    
    inline auto parse_json_number(String& x) {
        return [&x](auto& v) -> bool {
            auto u = v;
            if (match_json_number(u)) {
                x = v / u;
                v = u;
                return true;
            }
            return false;
        };
    }
    
    inline auto parse_json_number(auto& x) {
        return parse_number(x);
    }
    
    // parse into type-erased Value
    
    using wry::Value;
    
    bool parse_json_value(Value& x, auto& y);
    
    inline auto parse_json_null(Value& x) {
        return [&x](auto& v) {
            if (!match_json_null()(v))
                return false;
            x = nullptr;
        };
    }
    
    inline auto parse_json_boolean(Value& x) {
        return [&x](auto& v) {
            bool y;
            if (!parse_json_boolean(y)(v))
                return false;
            x = y;
            return true;
        };
    }
    
    inline auto parse_json_string(Value& x) {
        return [&x](auto& v) -> bool {
            String s;
            if (parse_json_string(s)(v)) {
                x = std::string_view((const char*)s.data(), s.chars.size());
                return true;
            }
            return false;
        };
    }
    
    inline auto parse_json_number(Value& x) {
        return [&x](auto& v) -> bool {
            String s;
            if (parse_json_number(s)(v)) {
                x = std::string_view((const char*)s.data(), s.chars.size());
                return true;
            }
            return false;
        };
    }
    
    inline auto parse_json_array(Value& x) {
        return [&x](auto& v) -> bool {
            //Array<Value> a;
            Value a = value_make_array();
            Value y;
            auto u = v;
            if (!match_json_array_begin()(u))
                return false;
            if (match_json_array_end()(u))
                goto success;
        expect_value:
            if (!parse_json_value(y, u))
                return false;
            // a.push_back(std::move(y));
            // value_insert_or_assign(a, k++, y);
            value_push_back(a, y);
            if (match_json_comma()(u))
                goto expect_value;
            if (!match_json_array_end()(u))
                return false;
        success:
            v = u;
            x = std::move(a);
            return true;
        };
    }
    
    inline auto parse_json_object(Value& x) {
        return [&x](auto& v) -> bool {
            // Table<String, Value> o;
            Value o = value_make_table();
            // String key;
            Value key;
            Value value;
            // std::pair<typename Table<String, Value>::iterator, bool> result;
            Value result;
            auto u = v;
            if (!match_json_object_begin()(u))
                return false;
            if (match_json_object_end()(u))
                goto success;
        expect_key:
            if (!parse_json_string(key)(u))
                return false;
            if (!match_json_colon()(u))
                return false;
            if (!parse_json_value(value, u))
                return false;
            // result = o.emplace(std::move(key), std::move(value));
            result = value_insert_or_assign(o, key, value);
            // if (!result.second) // duplicate key
            if (!value_is_null(result))
                return false;
            if (match_json_comma()(u))
                goto expect_key;
            if (!match_json_object_end()(u))
                return false;
        success:
            v = u;
            x = std::move(o);
            return true;
        };
    }
    
    inline auto parse_json_value(Value& x) {
        return [&x](auto& v) -> bool {
            return match_and(match_json_whitespace(),
                             match_or(parse_json_string(x),
                                      parse_json_number(x),
                                      parse_json_object(x),
                                      parse_json_array(x),
                                      parse_json_boolean(x),
                                      parse_json_null(x)))(v);
        };
    }
    
    bool parse_json_value(Value& x, auto& y) {
        return parse_json_value(x)(y);
    }
    
    
    
    
    
    
    
    
    
    
    
    // Rust Serde inspired [de]serialization
    
    using rust::usize;
    
    struct Error {
        // ?
    };
    
    struct ValueVisitor {
        
        using Value = Value;
        
        template<typename E>
        Value visit_none() {
            return Value();
        }
        
        template<typename E>
        Value visit_bool(bool x) {
            return Value(x);
        }
        
        template<typename E>
        Value visit_int8_t(int8_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_int16_t(int16_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_int32_t(int32_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_int64_t(int64_t x) {
            return Value(x);
        }
        
        template<typename E>
        Value visit_uint8_t(uint8_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_uint16_t(uint16_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_uint32_t(uint32_t x) {
            return Value((int64_t) x);
        }
        
        template<typename E>
        Value visit_uint64_t(uint64_t x) {
            // return Value(x);
            return value_make_error();
        }
        
        template<typename E>
        Value visit_float32_t(float32_t x) {
            // return Value((double) x);
            return value_make_error();
        }
        
        template<typename E>
        Value visit_float64_t(float64_t x) {
            // return Value(x);
            return value_make_error();
        }
        
        template<typename E>
        Value visit_string(String x) {
            // return Value(std::move(x));
            return Value(std::string_view((char*)x.data(), x.chars.size()));
        }
        
        template<typename E>
        Value visit_string_view(StringView x) {
            // return Value(String(x));
            return Value(std::string_view((char*)x.chars.data(), x.chars.size()));
        }
        
        template<typename A>
        Value visit_seq(A&& accessor) {
            // Array<Value> y;
            // Value t = value_make_table();
            Value a = value_make_array();
            // Value key = 0;
            for (;;) {
                Option<Value> x(accessor.template next_element<Value>());
                if (x.is_some()) {
                    printf("got a seq element\n");
                    // y.push_back(std::move(x).unwrap());
                    // value_insert_or_assign(t, key++, std::move(x).unwrap());
                    value_push_back(a, std::move(x).unwrap());
                }
                else
                    return a;
            }
        }
        
        template<typename A>
        Value visit_map(A&& accessor) {
            // Table<String, Value> z;
            Value t = value_make_table();
            for (;;) {
                Option<std::pair<String, Value>> x(accessor.template next_entry<String, Value>());
                if (x.is_some()) {
                    // auto [at, flag] = z.insert(std::move(x).unwrap());
                    // if (!flag)
                        // throw ERANGE;
                    auto y = std::move(x).unwrap();
                    value_insert_or_assign(t, Value(std::string_view((char*)y.first.chars.data(), y.first.chars.size())), y.second);
                } else {
                    return t; // Value(std::move(z));
                }
            }
        }
        
        
    };
    
    // import deserialize for primitives, or it will be shadowed
    using wry::deserialize;
    
    template<typename D>
    Value deserialize(std::in_place_type_t<Value>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_any(ValueVisitor{});
    }
    
    // serialization
    
    struct serializer {
        
        String s;
        
        void serialize_bool(bool x) {
            s.append(x ? u8"true" : u8"false");
        }
        
        void serialize_i8(int8_t x) {
            return serialize_i64(x);
        }
        
        void serialize_i16(int16_t x) {
            return serialize_i64(x);
        }
        
        void serialize_i32(int32_t x) {
            return serialize_i64(x);
        }
        
        void serialize_i64(int64_t x) {
            s.chars.may_write_back(32);
            auto first = reinterpret_cast<char*>(s.chars._end);
            auto last = reinterpret_cast<char*>(s.chars._allocation_end);
            std::to_chars_result result = std::to_chars(first, last, x);
            if (result.ptr == last)
                throw result.ec;
            assert(result.ptr != first);
            s.chars.did_write_back(result.ptr - first);
            assert(s.chars._end != s.chars._allocation_end);
            *s.chars._end = 0;
        }
        
        void serialize_uint8_t(uint8_t x) {
            return serialize_uint64_t(x);
        }
        
        void serialize_uint16_t(uint16_t x) {
            return serialize_uint64_t(x);
        }
        
        void serialize_uint32_t(uint32_t x) {
            return serialize_uint64_t(x);
        }
        
        void serialize_uint64_t(uint64_t x) {
            s.chars.may_write_back(32);
            auto first = reinterpret_cast<char*>(s.chars._end);
            auto last = reinterpret_cast<char*>(s.chars._allocation_end);
            std::to_chars_result result = std::to_chars(first, last, x);
            if (result.ptr == last)
                throw result.ec;
            assert(result.ptr != first);
            s.chars.did_write_back(result.ptr - first);
            assert(s.chars._end != s.chars._allocation_end);
            *s.chars._end = 0;
        }
        
        void serialize_float32_t(float32_t x) {
            return serialize_float64_t(x);
        }
        
        void serialize_float64_t(float64_t x) {
            s.chars.may_write_back(32);
            auto first = reinterpret_cast<char*>(s.chars._end);
            auto last = reinterpret_cast<char*>(s.chars._allocation_end);
            std::to_chars_result result = std::to_chars(first, last, x);
            if (result.ptr == last)
                throw result.ec;
            assert(result.ptr != first);
            s.chars.did_write_back(result.ptr - first);
            assert(s.chars._end != s.chars._allocation_end);
            *s.chars._end = 0;
        }
        
        void serialize_string(StringView x) {
            s.push_back(u8'\"');
            s.append(x);
            s.push_back(u8'\"');
        }
        
        struct SerializeSeq {
            
            serializer* _context;
            bool _need_delimiter = false;
            
            template<typename T>
            void serialize_element(T&& x) {
                if (_need_delimiter)
                    _context->s.push_back(u8',');
                _need_delimiter = true;
                serialize(std::forward<T>(x), *_context);
            }
            
            void end() {
                _context->s.push_back(u8']');
            }
            
        };
        
        SerializeSeq serialize_seq(Option<size_type>) {
            s.push_back(u8'[');
            return SerializeSeq{this};
        }
        
    };
    
    struct deserializer {
        
        using Error = Error;
        
        StringView& v;
        
        struct SeqAccess {
            
            deserializer* _parent;
            Option<usize> _size_hint;
            bool _expect_delimiter = false;
            
            template<typename T>
            Option<T> next_element() {
                auto& v = _parent->v;
                if (match_json_array_end()(v))
                    return None();
                if (_expect_delimiter && !match_json_comma()(v))
                    throw ERANGE;
                auto x = wry::deserialize<T>(*_parent);
                _expect_delimiter = true;
                return Some(std::move(x));
            }
            
            Option<usize> size_hint() const {
                return _size_hint;
            }
            
        };
        
        struct MapAccess {
            
            deserializer* _parent;
            Option<usize> _size_hint;
            bool _expect_delimiter = false;
            
            template<typename K, typename T>
            Option<std::pair<K, T>> next_entry() {
                auto& v = _parent->v;
                if (match_json_object_end()(v))
                    return None();
                if (_expect_delimiter && match_json_comma()(v))
                    throw ERANGE;
                using wry::deserialize;
                K key = deserialize<K>(*_parent);
                if (!match_json_colon()(v))
                    throw ERANGE;
                T value = deserialize<T>(*_parent);
                _expect_delimiter = true;
                return std::make_pair(std::move(key), std::move(value));
            }
            
            Option<usize> size_hint() const {
                return _size_hint;
            }
            
        };
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_any(V&& visitor) {
            
            match_json_whitespace()(v);
            
            if (match_string("null")(v))
                return std::forward<V>(visitor).visit_none();
            
            if (bool x = {}; parse_json_boolean(x)(v))
                return std::forward<V>(visitor).visit_bool(x);
            
            if (float64_t x = {}; parse_number(x)(v))
                return std::forward<V>(visitor).visit_float64_t(x);
            
            if (String x; parse_json_string(x)(v.chars))
                return std::forward<V>(visitor).visit_string(x);
            
            if (match_json_array_begin()(v))
                return std::forward<V>(visitor).visit_seq(SeqAccess{this});
            
            if (match_json_object_begin()(v))
                return std::forward<V>(visitor).visit_map(MapAccess{this});
            
            throw ERANGE;
            
        }
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_bool(V&& visitor) {
            match_json_whitespace()(v);
            if (v.empty())
                throw ERANGE;
            bool x = {};
            if (!parse_json_boolean(x)(v))
                throw ERANGE;
            return std::forward<V>(visitor).visit_bool(x);
        }
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_int64_t(V&& visitor) {
            match_json_whitespace()(v);
            int64_t x;
            if (!parse_number(x)(v))
                throw ERANGE;
            return std::forward<V>(visitor).visit_int64_t(x);
        }
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_string(V&& visitor) {
            match_json_whitespace()(v);
            String x;
            if (!parse_json_string(x)(v.chars))
                throw ERANGE;
            return std::forward<V>(visitor).visit_string(x);
        }
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_seq(V&& visitor) {
            if (!match_json_array_begin()(v))
                throw ERANGE;
            return std::forward<V>(visitor).visit_seq(SeqAccess{this});
        }
        
    };
    
    template<typename T> T from_string(String s) {
        StringView v = s;
        return wry::deserialize<T>(deserializer{v});
    }
    
    template<typename T> T from_file(const std::filesystem::path& name) {
        String s = string_from_file(name);
        StringView v = s;
        return wry::deserialize<T>(deserializer{v});
    }
    
} // namespace wry::json

#endif /* json_hpp */
