//
//  json.cpp
//  client
//
//  Created by Antony Searle on 23/10/19.
//  Copyright © 2019 Antony Searle. All rights reserved.
//

#if 0

#include <utility>
#include <cstdlib>
#include <sstream>

#include "debug.hpp"
#include "json.hpp"
#include "table.hpp"
#include "array.hpp"

namespace wry {
    
    // these methods deserialize json to type-erased values
    
    // correspond roughly to deserialize_any
    
    struct _json_value {
        
        [[noreturn]] static void unimplemented() { throw 0; }
        
        virtual ~_json_value() = default;
        virtual size_t size() const { unimplemented(); }
        virtual json const& at(size_t) const { unimplemented(); }
        virtual json const& at(string_view) const { unimplemented(); }
        virtual string_view as_string() const  { unimplemented(); }
        virtual double as_number() const  { unimplemented(); }
        virtual bool as_bool() const { unimplemented(); }
        virtual table<String, json> const& as_object() const { unimplemented(); }
        virtual Array<json> const& as_array() const { unimplemented(); }
        virtual bool is_string() const { unimplemented(); }
        virtual bool is_number() const { unimplemented(); }
        virtual bool is_array() const { unimplemented(); }
        virtual bool is_object() const { unimplemented(); }
        static _json_value* from(string_view&);
        virtual String debug() const = 0;
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
    table<String, json> const& json::as_object() const { return _ptr->as_object(); }
    Array<json> const& json::as_array() const { return _ptr->as_array(); }
    bool json::as_bool() const { return _ptr->as_bool(); }

    long json::as_long() const {
        double a = _ptr->as_number();
        long b = (long) a;
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
        assert(v.front() == '\"');
        ++v;
        auto c = v.begin();
        while (*c != '\"') {
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
        v.a._ptr = (char*) q;
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
        
        Array<json> _array;
        
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
        
        const Array<json> & as_array() const override {
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
    
    struct _json_bool : _json_value {
        
        bool _bool;
        
        explicit _json_bool(bool b) : _bool(b) {}
        
        virtual double as_number() const override {
            return _bool;
        }
        
        static _json_bool* from(string_view& v) {
            if (match_string("false")(v))
                return new _json_bool(false);
            if (match_string("true")(v))
                return new _json_bool(true);
            return nullptr;
        }
        
        virtual string debug() const override {
            return _bool ? "true" : "false";
        }
        
        virtual _json_bool* clone() const override {
            return new _json_bool(_bool);
        }
        
    };
    
    struct _json_null : _json_value {
        
        virtual double as_number() const override {
            return 0;
        }
        
        static _json_null* from(string_view& v) {
            if (match_string("null")(v))
                return new _json_null();
            return nullptr;
        }
        
        virtual string debug() const override {
            return "null";
        }
        
        virtual _json_null* clone() const override {
            return new _json_null();
        }
        
    };
    
    _json_value* _json_value::from(string_view& v) {
        while (iswspace(*v))
            ++v;
        
        switch (v.front()) {
            case '{': // object
                return _json_object::from(v);
            case '[': // array
                return _json_array::from(v);
            case '\"': // string
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
    
    /*
    
    void _json_expect_value(const char*& first, const char* last) {
        
        for (;;) {
            
            if (first == last)
                return;
            
            switch (*first) {
                case '\t':
                case '\n':
                case '\r':
                case ' ':
                    ++first;
                    continue;
                case '\"':
                    return _json_continue_string(++first, last);
                case '-':
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
                    return _json_continue_number(first, last);
                case '[':
                    return _json_continue_array(++first, last);
                    ;
                case 'f':
                    ;
                case 'n':
                    ;
                case 't':
                    ;
                case '{':
                    return _json_continue_object(++first, last);
                    ;
                default:
                    ;
            }
            
        }
        
        
    }
     
     */
    
    
} // namespace wry

namespace wry {

    /*
    bool match2_json_number(string_view& v);

    bool match2_json_string(string_view& v);
    
    bool match2_json_true(string_view& v) {
        uint ch = 0;
        if (!v)
            return false;
        ch = *v;
        if (ch == )
    }
    
    
    bool match2_json_false(string_view& v);
    bool match2_json_null(string_view& v);
    bool match2_json_array(string_view& v);
    bool match2_json_object(string_view& v);
    
    bool match2_json_element(string_view& v) {
        
        if (!v)
            return false;
        
        uint ch = *v;
                        
        if (!isuchar(ch))
            return false;

        if (isdigit(ch))
            [[clang::musttail]] return match2_json_number(v);

        ++v;
        
        if (ch == '-')
            [[clang::musttail]] return match2_json_number(v);
        
        if (is_json_whitespace(ch))
            [[clang::musttail]] return match2_json_element(v);
        
        if (ch == '\"')
            [[clang::musttail]] return match2_json_string(v);
        
        if (ch == '[')
            [[clang::musttail]] return match2_json_array(v);

        if (ch == 'f')
            [[clang::musttail]] return match2_json_false(v);

        if (ch == 't')
            [[clang::musttail]] return match2_json_true(v);

        if (ch == 'n')
            [[clang::musttail]] return match2_json_null(v);

        if (ch == '{')
            [[clang::musttail]] return match2_json_array(v);
        
        --v;
        return false;

    }
     */
    
    /*
    
    bool match2_json_string(string_view& v) {
        
        string_view u = v;
        
        for (;;) {
            
            if (!u)
                // unterminated string
                return false;
        
            uint ch = *u;
            ++u;

            if (ch == '"') {
                // string terminates
                v = u;
                return true;
            }
            
            if (isuchar(ch) && iscntrl(ch))
                // cntrl characters not permitted in string
                return false;
            
            if (ch != '\\')
                // regular character
                continue;
            
            // escape sequence
            
            if (!u)
                return false;
            ch = *u;
            ++u;

            switch (ch) {
                case '\"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                    // regular escape sequence
                    continue;
                case 'u':
                    // utf-16 character
                    for (int i = 0; i != 4; ++i) {
                        if (!u)
                            return false;
                        ch = *u;
                        ++u;
                        if (!(isuchar(ch) && !isxdigit(ch)))
                            return false;
                    }
                    continue;
                default:
                    return false;
            }
            
        }
        
    }
    
    bool match2_json_true(string_view& v) {
        string_view u = v;
        ++u;
        if (!u || (*u != 'r'))
            return  false;
        ++u;
        if (!u || (*u != 'u'))
            return  false;
        ++u;
        if (!u || (*u != 'e'))
            return  false;
        ++u;
        v = u;
        return true;
    }

    bool match2_json_false(string_view& v) {
        string_view u = v;
        if (!u || (*u != 'a'))
            return  false;
        ++u;
        if (!u || (*u != 'l'))
            return  false;
        ++u;
        if (!u || (*u != 's'))
            return  false;
        ++u;
        if (!u || (*u != 'e'))
            return  false;
        ++u;
        v = u;
        return true;
    }

    bool match2_json_null(string_view& v) {
        string_view u = v;
        if (!u || (*u != 'u'))
            return  false;
        ++u;
        if (!u || (*u != 'l'))
            return  false;
        ++u;
        if (!u || (*u != 'l'))
            return  false;
        ++u;
        v = u;
        return true;
    }
    
    bool match2_json_element(string_view& v) {
        
        string_view u = v;
        
        for (;;) {
            
            if (!u)
                return false;
        
            uint ch = *u;
            
            if (is_json_whitespace(ch)) {
                ++u;
                continue;
            }
            
            switch (ch) {
                case '\"':
                case '[':
                case '{':
                default:
                    return false;
            }
                
        }
    }
    
    using uchars = Array<uchar>;
    
    bool json_continue_string_escape_u(const uchar*& a, const uchar* b, uchars& s);
    bool json_continue_string_escape(const uchar*& a, const uchar* b, uchars& s);
    bool json_continue_string(const uchar*& a, const uchar* b, uchars& s);

    bool json_continue_escape_sequence_u(const uchar*& a, const uchar* b, uchars& s) {
        uint u = 0;
        for (int i = 0; i != 4; ++i) {
            if (a == b)
                return false;
            uchar c = *a;
            u <<= 4;
            if (!isxdigit(c))
                return false;
            ++a;
            u += c - (isdigit(c) ? '0' : (isupper(c) ? 'A' : 'a') - 10);
        }
        s.push_back(u);
        [[clang::musttail]] return json_continue_string(a, b, s);
    }

    bool json_continue_escape_sequence(const uchar*& a, const uchar* b, uchars& s) {
        
        if (a == b)
            return false;
        
        uchar c = *a;
        ++a;
        
        
        switch (*a) {
            case 'u':
                [[clang::musttail]] return json_continue_string_escape_u(a, b, s);
            case '\"':
                s.push_back('\"');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case '\\':
                s.push_back('\\');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case '/':
                s.push_back('/');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case 'b':
                s.push_back('\b');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case 'f':
                s.push_back('\f');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case 'n':
                s.push_back('\n');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case 'r':
                s.push_back('\r');
                [[clang::musttail]] return json_continue_string(a, b, s);
            case 't':
                s.push_back('\t');
                [[clang::musttail]] return json_continue_string(a, b, s);
            default:
                return false;
        }

    }

    
    bool json_continue_string(const uchar*& a, const uchar* b, uchars& s) {
        
        if (a == b)
            return false;
        
        uchar c = *a;
        
        if (iscntrl(c))
            return false;
        
        ++a;
        
        if (c == '\"')
            return true;
        
        if (c == '\\')
            [[clang::musttail]] return json_continue_escape_sequence(a, b, s);
        
        s.push_back(c);
        [[clang::musttail]] return json_continue_string(a, b, s);
        
    }
    
    bool json_begin_number(const uchar*& a, const uchar* b);

    bool json_begin_elements(const uchar*& a, const uchar* b);

    bool json_continue_array(const uchar*& a, const uchar* b) {
        if (a == b)
            return false;
        unsigned char c = *a;
        switch (c) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
                ++a;
                [[clang::musttail]] return json_continue_array(a, b);
            case ']':
                ++a;
                return true;
            default:
                [[clang::musttail]] return json_begin_elements(a, b);
        }
        
    }
    
    bool json_begin_element(const unsigned char*& a, const unsigned char* b) {
        
        if (a == b)
            return false;
        unsigned char c = *a;
        
        switch (c) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
                ++a;
                [[clang::musttail]] return json_begin_element(a, b);
            case '\"':
                ++a;
                [[clang::musttail]] return json_continue_string(a, b);
            case '-':
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
                [[clang::musttail]] return json_begin_number(a, b);
            case '[':
                [[clang::musttail]] return parse2_json_array(a, b);
            case 'f':
                [[clang::musttail]] return parse2_json_false(a, b);
            case 'n':
                [[clang::musttail]] return parse2_json_null(a, b);
            case 't':
                [[clang::musttail]] return parse2_json_true(a, b);
            case '{':
                [[clang::musttail]] return parse2_json_object(a, b);

        }
        
        
        
        
    }
     
     */
    
    void json_parse_whitespace(ArrayView<const char>& a) {
        for (;;) {
            if (a.empty())
                return;
            char c = a.front();
            switch (c) {
                case '\t':
                case '\n':
                case '\r':
                case ' ':
                    a.pop_front();
                    continue;
                default:
                    return;
            }
        }
    }

    
    int xdigittoint(int c) {
        static const char d[128] = {
            0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,7, 8,9,0,0,0,0,0,0,
            0,10,11,12,13,14,15,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
            0,10,11,12,13,14,15,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
        };
        assert(('0' <= c) && (c <= 'f'));
        return d[c];
    }
    
    bool json_parse_string(ArrayView<const char>& a, Array<char>& z) {
        for(;;)  {
            if (a.empty())
                return false;
            char c = a.front();
            if (isascii(c) && iscntrl(c))
                return false;
            if (c == '\"') {
                z.push_back('\0');
                return true;
            }
            if (c == '\\') {
                a.pop_front();
                if (a.empty())
                    return false;
                c = a.front();
                switch (c) {
                    case '\"':
                        c = '\"';
                        break;
                    case '\\':
                        c = '\\';
                        break;
                    case '/':
                        c = '/';
                        break;
                    case 'b':
                        c = '\b';
                        break;
                    case 'f':
                        c = '\f';
                        break;
                    case 'n':
                        c = '\n';
                        break;
                    case 'r':
                        c = '\r';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case 'u': {
                        uint d = 0;
                        for (int i = 0; i != 4; ++i) {
                            a.pop_front();
                            if (a.empty())
                                return false;
                            c = a.front();
                            if (!isascii(c) || !isxdigit(c))
                                return false;
                            d <<= 4;
                            d |= xdigittoint(c);
                        }
                        char e[4] = {};
                        const char* f = utf8_encode(d, e);
                        assert(e < f);
                        for (const char* g = e;; ++g) {
                            c = *g;
                            ++g;
                            if (g == f)
                                break;
                            z.push_back(c);
                        }
                    }
                    default:
                        return false;
                }
            }
            z.push_back(c);
        }
    }
            
    bool json_parse_element(ArrayView<const char>& a, json&);
    
    bool json_parse_array(ArrayView<const char>& a, Array<json>& result) {
        json value;
        char c = 0;
        
        json_parse_whitespace(a);
        
        // expect value or end
        if (a.empty())
            return false;
        c = a.front();
        if (c == ']')
            return true;
        
        for (;;) {
            
            // expect value
            if (!json_parse_element(a, value))
                return false;
            result.push_back(std::move(value));
            
            json_parse_whitespace(a);
            
            // expect comma or end
            if (a.empty())
                return false;
            c = a.front();
            if (c == ']') {
                return true;
            }
            if (c != ',')
                return false;
            a.pop_front();
            json_parse_whitespace(a);
            
        }
        
    }
    
    bool json_parse_object(ArrayView<const char>& a, table<string, json>& result) {
        
        Array<char> chars;
        json value;
        char c;
        
        json_parse_whitespace(a);
        
        // expect string or end
        if (a.empty())
            return false;
        c = a.front();
        if (c == '}')
            return true;
        
        for (;;) {
            // expect string
            if (c != '\"')
                return false;
            a.pop_front();
            if (!json_parse_string(a, chars))
                return false;
            // discard closing double quote
            assert(!a.empty());
            c = a.front();
            assert(c == '\"');
            a.pop_front();
            json_parse_whitespace(a);
            // expect colon
            if (a.empty())
                return false;
            c = a.front();
            if (c != ':')
                return false;
            json_parse_whitespace(a);
            // expect element
            json_parse_element(a, value);
            string key(std::move(chars));
            auto [where, did_emplace] = result.emplace(string(std::move(chars)), std::move(value));
            if (!did_emplace)
                return false; // duplicate key; this is our only nonlocal failure mode?
            json_parse_whitespace(a);
            // expect comma or end
            if (a.empty())
                return false;
            c = a.front();
            if (c == '}')
                return true;
            if (c != ',')
                return false;
            a.pop_front();
            json_parse_whitespace(a);
        }
    }
    
    bool json_parse_false(ArrayView<const char>& a, json& value) {
        const char* z = "false";
        for (;;) {
            if (!*z)
                return true;
            if (a.empty())
                return false;
            char c = a.front();
            if (c != *z)
                return false;
            a.pop_front();
            ++z;
        }
    }

    bool json_parse_null(ArrayView<const char>& a, json& value) {
        const char* z = "null";
        for (;;) {
            if (!*z)
                return true;
            if (a.empty())
                return false;
            char c = a.front();
            if (c != *z)
                return false;
            a.pop_front();
            ++z;
        }
    }

    bool json_parse_true(ArrayView<const char>& a, json& value) {
        const char* z = "true";
        for (;;) {
            if (!*z)
                return true;
            if (a.empty())
                return false;
            char c = a.front();
            if (c != *z)
                return false;
            a.pop_front();
            ++z;
        }
    }

    bool json_parse_element(ArrayView<const char>& a, json& value) {

        // expect element
        assert(!a.empty());
        char c = a.front();
        switch (c) {
            case '\"': {
                a.pop_front();
                Array<char> z;
                if (!json_parse_string(a, z))
                    return false;
                a.pop_back();
            }
            case '-':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': {
                double z = 0;
                if (!json_parse_number(a, z))
                    return false;
                return true;
            }
            case '[': {
                a.pop_front();
                Array<json> z;
                if (!json_parse_array(a, z))
                    return false;
                a.pop_back();
                return true;
            }
            case 'f':
                return json_parse_false(a, value);
            case 'n':
                return json_parse_null(a, value);
            case 't':
                return json_parse_true(a, value);
            case '{': {
                a.pop_front();
                table<string, json> z;
                if (!json_parse_object(a, z))
                    return false;
                a.pop_back();
                return true;
            }
            default:
                return false;
        }
        
    }
    
    bool json_parse_json(ArrayView<const char>& a, json& value) {
        json_parse_whitespace(a);
        if (!json_parse_element(a, value))
            return false;
        json_parse_whitespace(a);
        return true;
    }
    
    
    
    
    
} // namespace wry

#endif
