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

namespace wry {
    
    string _string_from_file(string_view);
    
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
        double as_number() const;
        table<string, json> const& as_object() const;
        array<json> const& as_array() const;
        
        i64 as_i64() const;
        
    }; // json
    
    std::ostream& operator<<(std::ostream&, json const&);
    
} // namespace manic

#endif /* json_hpp */
