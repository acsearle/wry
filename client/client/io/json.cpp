//
//  json.cpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#include <sstream>

#include "debug.hpp"
#include "json.hpp"
#include "test.hpp"

namespace wry::json {

    // these methods deserialize json to type-erased values
    
    // correspond roughly to deserialize_any
    
    struct _json_value {
        
        [[noreturn]] static void unimplemented() { throw 0; }
        
        virtual ~_json_value() = default;
        virtual size_t size() const { unimplemented(); }
        virtual Json const& at(size_t) const { unimplemented(); }
        virtual Json const& at(StringView) const { unimplemented(); }
        virtual StringView as_string() const  { unimplemented(); }
        virtual double as_number() const  { unimplemented(); }
        virtual bool as_bool() const { unimplemented(); }
        virtual Table<String, Json> const& as_object() const { unimplemented(); }
        virtual ContiguousDeque<Json> const& as_array() const { unimplemented(); }
        virtual bool is_string() const { return false; }
        virtual bool is_number() const { return false; }
        virtual bool is_array() const { return false; }
        virtual bool is_object() const { return false; }
        virtual bool is_bool() const { return false; }
        virtual bool is_null() const { return false; }
        static _json_value* from(StringView&);
        virtual String debug() const = 0;
        virtual _json_value* clone() const = 0;
        
    }; // _json_value
    
    Json::Json(Json const& x)
    : _ptr(x._ptr ? x._ptr->clone() : nullptr) {
    }
    
    Json::~Json() { delete _ptr; }
    
    Json& Json::operator=(Json const& x) {
        Json tmp(x);
        using std::swap;
        swap(*this, tmp);
        return *this;
    }
    
    size_t Json::size() const { return _ptr->size(); }
    
    Json const& Json::operator[](size_t i) const { return _ptr->at(i); }
    Json const& Json::operator[](StringView s) const { return _ptr->at(s); }
    
    StringView Json::as_string() const { return _ptr->as_string(); }
    double Json::as_number() const { return _ptr->as_number(); }
    Table<String, Json> const& Json::as_object() const { return _ptr->as_object(); }
    ContiguousDeque<Json> const& Json::as_array() const { return _ptr->as_array(); }
    bool Json::as_bool() const { return _ptr->as_bool(); }

    bool Json::is_string() const { return _ptr->is_string(); }
    bool Json::is_number() const { return _ptr->is_number(); }
    bool Json::is_array() const { return _ptr->is_array(); }
    bool Json::is_object() const { return _ptr->is_object(); }
    bool Json::is_bool() const { return _ptr->is_bool(); }
    bool Json::is_null() const { return _ptr->is_null(); }

    long Json::as_long() const {
        double a = _ptr->as_number();
        long b = (long) a;
        assert(((double) b) == a);
        return b;
    }
    
    Json Json::from(StringView& v) {
        return Json(_json_value::from(v));
    }
    
    Json Json::from(StringView&& v) {
        StringView u{v};
        _json_value* p = _json_value::from(u);
        while (!u.empty() && is_json_whitespace(u.front()))
            u.pop_front();
        assert(u.empty());
        return Json(p);
    }
    
    std::ostream& operator<<(std::ostream& a, Json const& b) {
        return a << b._ptr->debug() << std::endl;
    }
    
    double _number_from(StringView& v) {
        char* q;
        double d = std::strtod(v.chars.begin(), &q);
        v.chars._begin = q;
        return d;
    }
    
    struct _json_object : _json_value {
        
        Table<String, Json> _table;

        bool is_object() const override { return true; }

        virtual size_t size() const override {
            return _table.size();
        }
        
        virtual Json const& at(StringView key) const override {
            return _table.at(key);
        }
        
        static _json_object* from(StringView& v) {
            _json_object* p = new _json_object;
            while (is_json_whitespace(v.front())) v.pop_front();
            assert(v.front() == '{'); v.pop_front();
            while (is_json_whitespace(v.front())) v.pop_front();
            while (v.front() != '}') {
                // parse the key string
                String s;
                if (!parse_json_string(s)(v.chars)) return nullptr;
                while (is_json_whitespace(v.front())) v.pop_front();
                assert(v.front() == ':'); v.pop_front();
                // p->_table._assert_invariant();
                assert(!p->_table.contains(s));
                auto [i, f] = p->_table.emplace(s, Json(_json_value::from(v)));
                assert(f);
                //p->_table._assert_invariant();
                assert(p->_table.contains(s));
                while (is_json_whitespace(v.front())) v.pop_front();
                assert((v.front() == ',') || (v.front() == '}'));
                if (v.front() == ',') {
                    v.pop_front(); while (is_json_whitespace(v.front())) v.pop_front();
                }
            }
            v.pop_front();
            return p;
        }
        
        virtual String debug() const override {
            String s;
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
        
        const Table<String, Json> & as_object() const override {
            return _table;
        }
        
    };
    
    struct _json_array : _json_value {
        
        ContiguousDeque<Json> _array;

        bool is_array() const override { return true; }

        virtual Json const& at(size_t i) const override {
            return _array[i];
        }
        
        virtual size_t size() const override {
            return _array.size();
        }
        
        static _json_array* from(StringView& v) {
            _json_array* p = new _json_array;
            while (is_json_whitespace(v.front())) v.pop_front();
            assert(v.front() == '['); v.pop_front();
            while (is_json_whitespace(v.front())) v.pop_front();
            while (v.front() != ']') {
                p->_array.push_back(Json(_json_value::from(v)));
                while (is_json_whitespace(v.front())) v.pop_front();
                assert((v.front() == ',') || (v.front() == ']'));
                if (v.front() == ',') {
                    v.pop_front(); while (is_json_whitespace(v.front())) v.pop_front();
                }
            }
            v.pop_front();
            return p;
        }
        
        virtual String debug() const override {
            String s;
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
        
        const ContiguousDeque<Json> & as_array() const override {
            return _array;
        }
        
    };
    
    struct _json_string : _json_value {
        
        String _string;
        
        explicit _json_string(StringView v) : _string(v) {}
        explicit _json_string(String&& s) : _string(std::move(s)) {}

        bool is_string() const override { return true; }

        virtual StringView as_string() const override {
            return _string;
        }
        
        static _json_string* from(StringView& v) {
            String s;
            if (!parse_json_string(s)(v.chars)) return nullptr;   // strips quotes, decodes escapes, advances v
            return new _json_string(std::move(s));
        }
        
        virtual String debug() const override {
            String s;
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

        bool is_number() const override { return true; }

        virtual double as_number() const override {
            return _number;
        }
        
        static _json_number* from(StringView& v) {
            return new _json_number(_number_from(v));
        }
        
        virtual String debug() const override {
            char s[32];
            return String(s, snprintf(s, 32, "%g", _number));
        }
        
        virtual _json_number* clone() const override {
            return new _json_number(_number);
        }
        
        
    };
    
    struct _json_bool : _json_value {
        
        bool _bool;
        
        explicit _json_bool(bool b) : _bool(b) {}

        bool is_bool() const override { return true; }

        virtual double as_number() const override {
            return _bool;
        }

        virtual bool as_bool() const override {
            return _bool;
        }

        static _json_bool* from(StringView& v) {
            if (match_zstr("false")(v))
                return new _json_bool(false);
            if (match_zstr("true")(v))
                return new _json_bool(true);
            return nullptr;
        }
        
        virtual String debug() const override {
            return String(_bool ? "true" : "false");
        }
        
        virtual _json_bool* clone() const override {
            return new _json_bool(_bool);
        }
        
    };
    
    struct _json_null : _json_value {

        bool is_null() const override { return true; }

        virtual double as_number() const override {
            return 0;
        }
        
        static _json_null* from(StringView& v) {
            if (match_zstr("null")(v))
                return new _json_null();
            return nullptr;
        }
        
        virtual String debug() const override {
            return String("null");
        }
        
        virtual _json_null* clone() const override {
            return new _json_null();
        }
        
    };
    
    _json_value* _json_value::from(StringView& v) {
        while (is_json_whitespace(v.front()))
            v.pop_front();
        
        switch (v.front()) {
            case '{': // object
                return _json_object::from(v);
            case '[': // array
                return _json_array::from(v);
            case '\"': // String
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
                return _json_bool::from(v);
            case 'f': // false
                return _json_bool::from(v);
            case 'n': // null
                return _json_null::from(v);
            default:
                assert(false);
        }
        
        return nullptr;

    }

    // Round-trip smoke test for the Json DOM (the Serde path is covered
    // elsewhere; this exercises Json::from end to end: object/key parse,
    // number, nested array, bool, null, and escape decoding).
    define_test("json")
    {
        Json j = Json::from(StringView("{\"a\":1,\"b\":[true,null,\"x\\\"y\"]}"));

        assert(j.is_object());
        assert(!j.is_array());
        assert(j.size() == 2);
        assert(j[StringView("a")].is_number());
        assert(j[StringView("a")].as_number() == 1.0);

        Json const& b = j[StringView("b")];
        assert(b.is_array());
        assert(b.size() == 3);
        assert(b[0].is_bool() && b[0].as_bool() == true);
        assert(b[1].is_null());
        assert(b[2].is_string() && b[2].as_string() == StringView("x\"y")); // escape decoded

        co_return;
    };

} // namespace wry::json
