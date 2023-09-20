//
//  json.hpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#ifndef json_hpp
#define json_hpp

#include <utility>

#include "string.hpp"
#include "table.hpp"
#include "vector.hpp"
#include "parse.hpp"

namespace wry {
    
    inline auto match_json_whitespace() {
        return match_star(match_from(" \n\r\t"));
    }
    
    inline auto match_json_number() {
        return match_and(match_optional(match_character('-')),
                         match_or(match_character('0'),
                                  match_and(match_nonzero_digit(),
                                            match_digit())),
                         match_optional(match_fractional_digits(),
                                        match_exponent()));
    }
    
    inline auto match_any_codepoint_except_double_quote_or_reverse_solidus_or_control_characters() {
        return match_predicate([](int character) {
            return (!isuchar(character)
                    || (!iscntrl(character)
                        && character != '\"'
                        && character != '\\'));
        });
    }
    
    inline auto match_json_string() {
        return match_and(match_character('\"'),
                         match_star(match_or(match_any_codepoint_except_double_quote_or_reverse_solidus_or_control_characters(),
                                             match_and(match_character('\\'),
                                                       match_or(match_from("\"\\/bfnrt"),
                                                                match_and(match_character('u'),
                                                                          match_count(match_xdigit(), 4)))))),
                         match_character('\"'));
    }
    
    /*
    inline auto match_json_value() {
        return match_and(match_json_whitespace(),
                         match_or(match_json_string(),
                                  match_json_number(),
                                  match_json_object(),
                                  match_json_array(),
                                  match_string("true"),
                                  match_string("false"),
                                  match_string("null")),
                         match_json_whitespace());
    }
     */
    
    
    
    
    
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
    
    void swap(json& x, json& y) {
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

#endif /* json_hpp */
