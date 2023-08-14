//
//  json.cpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#include <cctype>
#include <utility>
#include <cstdlib>
#include <sstream>

#include "debug.hpp"
#include "json.hpp"
#include "table.hpp"
#include "array.hpp"

namespace wry {
    
    string _string_from_file(string_view v) {
        string s(v);
        FILE* f = fopen(s.c_str(), "rb");
        assert(f);
        s.clear();
        int c;
        while ((c = fgetc(f)) != EOF)
            s.push_back(c);
        fclose(f);
        return s;
    }
    
    struct _json_value {
        
        [[noreturn]] static void unimplemented() { throw 0; }
        
        virtual ~_json_value() = default;
        virtual size_t size() const { unimplemented(); }
        virtual json const& at(size_t) const { unimplemented(); }
        virtual json const& at(string_view) const { unimplemented(); }
        virtual string_view as_string() const  { unimplemented(); }
        virtual double as_number() const  { unimplemented(); }
        virtual table<string, json> const& as_object() const { unimplemented(); }
        virtual array<json> const& as_array() const { unimplemented(); }
        virtual bool is_string() const { unimplemented(); }
        virtual bool is_number() const { unimplemented(); }
        virtual bool is_array() const { unimplemented(); }
        virtual bool is_object() const { unimplemented(); }
        static _json_value* from(string_view&);
        virtual string debug() const = 0;
        virtual _json_value* clone() const = 0;
        
    }; // _json_value
    
    json::json(json const& x)
    : _ptr(x._ptr ? x._ptr->clone() : nullptr) {
    }
    
    json::~json() { delete _ptr; }
    
    json& json::operator=(json const& x) {
        json tmp(x);
        using std::swap;
        swap(*this, tmp);
        return *this;
    }
    
    size_t json::size() const { return _ptr->size(); }
    
    json const& json::operator[](size_t i) const { return _ptr->at(i); }
    json const& json::operator[](string_view s) const { return _ptr->at(s); }
    
    string_view json::as_string() const { return _ptr->as_string(); }
    double json::as_number() const { return _ptr->as_number(); }
    table<string, json> const& json::as_object() const { return _ptr->as_object(); }
    array<json> const& json::as_array() const { return _ptr->as_array(); }
    
    i64 json::as_i64() const {
        double a = _ptr->as_number();
        i64 b = (i64) a;
        assert(((double) b) == a);
        return b;
    }
    
    
    json json::from(string_view& v) {
        
        return json(_json_value::from(v));
        
    }
    
    json json::from(string_view&& v) {
        string_view u{v};
        _json_value* p = _json_value::from(u);
        while (u && iswspace(u.front()))
            ++u;
        assert(u.empty());
        return json(p);
    }
    
    std::ostream& operator<<(std::ostream& a, json const& b) {
        return a << b._ptr->debug() << std::endl;
    }
    
    
    string _string_from(string_view& v) {
        while (iswspace(v.front()))
            ++v;
        assert(v.front() == '"');
        ++v;
        auto c = v.begin();
        while (*c != '"') {
            assert(c != v.end());
            ++c; // bug: account for escape characters
        }
        string s(v.begin(), c);
        v.a = ++c;
        return s;
    }
    
    double _number_from(string_view& v) {
        char* q;
        double d = std::strtod((char const*) v.a._ptr, &q);
        v.a._ptr = (u8*) q;
        return d;
    }
    
    struct _json_object : _json_value {
        
        table<string, json> _table;
        
        virtual size_t size() const override {
            return _table.size();
        }
        
        virtual json const& at(string_view key) const override {
            return _table.at(key);
        }
        
        static _json_object* from(string_view& v) {
            _json_object* p = new _json_object;
            while (iswspace(*v)) ++v;
            assert(*v == '{'); ++v;
            while (iswspace(*v)) ++v;
            while (*v != '}') {
                auto s = _string_from(v);
                while (iswspace(*v)) ++v;
                assert(*v == ':'); ++v;
                // p->_table._assert_invariant();
                assert(!p->_table.contains(s));
                auto [i, f] = p->_table.emplace(s, json(_json_value::from(v)));
                assert(f);
                //p->_table._assert_invariant();
                assert(p->_table.contains(s));
                while (iswspace(*v)) ++v;
                assert((*v == ',') || (*v == '}'));
                if (v.front() == ',') {
                    ++v; while (iswspace(*v)) ++v;
                }
            }
            ++v;
            return p;
        }
        
        virtual string debug() const override {
            string s;
            s.append("{ ");
            for (auto const& [k, v] : _table) {
                s.append(k);
                s.append(" : ");
                s.append(v._ptr->debug());
                s.append(", ");
            }
            s.append("}");
            return s;
        }
        
        virtual _json_object* clone() const override {
            auto p = new _json_object;
            // p->_table.reserve(_table.size());
            for (auto&& [key, value] : _table) {
                p->_table.emplace(key, value);
            }
            return p;
        }
        
        const table<string, json> & as_object() const override {
            return _table;
        }
        
    };
    
    struct _json_array : _json_value {
        
        array<json> _array;
        
        virtual json const& at(size_t i) const override {
            return _array[i];
        }
        
        virtual size_t size() const override {
            return _array.size();
        }
        
        static _json_array* from(string_view& v) {
            _json_array* p = new _json_array;
            while (iswspace(*v)) ++v;
            assert(*v == '['); ++v;
            while (iswspace(*v)) ++v;
            while (*v != ']') {
                p->_array.push_back(json(_json_value::from(v)));
                while (iswspace(*v)) ++v;
                assert((*v == ',') || (*v == ']'));
                if (*v == ',') {
                    ++v; while (iswspace(*v)) ++v;
                }
            }
            ++v;
            return p;
        }
        
        virtual string debug() const override {
            string s;
            s.append("[ ");
            for (auto const& v : _array) {
                s.append(v._ptr->debug());
                s.append(", ");
            }
            s.append("]");
            return s;
        }
        
        virtual _json_array* clone() const override {
            _json_array* p = new _json_array;
            p->_array = _array;
            return p;
        }
        
        const array<json> & as_array() const override {
            return _array;
        }
        
    };
    
    struct _json_string : _json_value {
        
        string _string;
        
        explicit _json_string(string_view v) : _string(v) {}
        explicit _json_string(string&& s) : _string(std::move(s)) {}
        
        virtual string_view as_string() const override {
            return _string.c_str();
        }
        
        static _json_string* from(string_view& v) {
            return new _json_string(_string_from(v));
        }
        
        virtual string debug() const override {
            string s;
            s.append("\"");
            s.append(_string);
            s.append("\"");
            return s;
        }
        
        _json_string* clone() const override {
            return new _json_string(_string);
        }
        
    };
    
    struct _json_number : _json_value {
        
        double _number;
        
        explicit _json_number(double d) : _number(d) {}
        
        virtual double as_number() const override {
            return _number;
        }
        
        static _json_number* from(string_view& v) {
            return new _json_number(_number_from(v));
        }
        
        virtual string debug() const override {
            char s[32];
            return string(s, snprintf(s, 32, "%g", _number));
        }
        
        virtual _json_number* clone() const override {
            return new _json_number(_number);
        }
        
        
    };
    
    
    
    _json_value* _json_value::from(string_view& v) {
        while (iswspace(*v)) ++v;
        
        switch (v.front()) {
            case '{': // object
                return _json_object::from(v);
            case '[': // array
                return _json_array::from(v);
            case '"': // string
                return _json_string::from(v);
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
                return _json_number::from(v);
            case 't': // true
            case 'f': // false
            case 'n': // null
            default:
                assert(false);
        }
        
        return nullptr;
        
    }
    
    
} // namespace wry
