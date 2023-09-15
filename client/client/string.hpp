//
//  string.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//



#ifndef string_hpp
#define string_hpp

#include "array.hpp"
#include "common.hpp"
#include "string_view.hpp"

namespace wry {
    
    struct string {
        
        // utf-8 string
        //
        // stored in a vector of uchar.  to permit efficient c_str(), we guarantee
        // that the allocation extends at least one uchar beyond end(), and that
        // *end() == 0; this often leads to a
        //
        //     _bytes.push_back(0)
        //     _bytes.pop_back()
        //
        // idiom
        
        array<uchar> _bytes;
        
        using const_iterator = utf8_iterator;
        using iterator = const_iterator;
        using value_type = uint;
        
        string() = default;
        
        string(char const* z) {
            assert(z);
            std::size_t n = std::strlen(z);
            _bytes.assign(z, z + n + 1);
            _bytes.pop_back();
        }
        
        string(char const* p, size_t n) {
            _bytes.reserve(n + 1);
            _bytes.assign(p, p + n);
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        string(char const* p, char const* q) {
            _bytes.reserve(q - p + 1);
            _bytes.assign(p, q);
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        string(string_view v) {
            _bytes.reserve(v.as_bytes().size() + 1);
            // _bytes = v.as_bytes();
            auto u = v.as_bytes();
            _bytes.assign(reinterpret_cast<const uchar*>(u.begin()),
                          reinterpret_cast<const uchar*>(u.end()));
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        explicit string(const_iterator a, const_iterator b) {
            _bytes.reserve(b._ptr - a._ptr + 1);
            _bytes.assign(a._ptr, b._ptr);
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        explicit string(array<uchar>&& bytes) : _bytes(std::move(bytes)) {
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        operator array_view<const uchar>() const {
            return array_view<const uchar>(_bytes.begin(), _bytes.end());
        }
        operator string_view() const { return string_view(begin(), end()); }
        
        const_iterator begin() const { return utf8_iterator{_bytes.begin()}; }
        const_iterator end() const { return utf8_iterator{_bytes.end()}; }
        
        uchar const* data() const { return _bytes.begin(); }
        
        array_view<const uchar> as_bytes() const {
            return array_view<const uchar>(_bytes.begin(), _bytes.end());
        }
        
        char const* c_str() const { return (char const*) _bytes.begin(); }
        
        void push_back(uint c) {
            if (c < 0x80) {
                _bytes.push_back(c);
            } else if (c < 0x800) {
                _bytes.push_back(0xC0 | ((c >>  6)       ));
                _bytes.push_back(0x80 | ((c      ) & 0x3F));
            } else if (c < 0x10000) {
                _bytes.push_back(0xC0 | ((c >> 12)       ));
                _bytes.push_back(0x80 | ((c >>  6) & 0x3F));
                _bytes.push_back(0x80 | ((c      ) & 0x3F));
            } else {
                _bytes.push_back(0xC0 | ((c >> 18)       ));
                _bytes.push_back(0x80 | ((c >> 12) & 0x3F));
                _bytes.push_back(0x80 | ((c >>  6) & 0x3F));
                _bytes.push_back(0x80 | ((c      ) & 0x3F));
            }
            _bytes.push_back(0);
            _bytes.pop_back();
        }

        void push_front(uint c) {
            _bytes.push_back(0);
            _bytes._reserve_front(4);
            if (c < 0x80) {
                _bytes.push_front(c);
            } else if (c < 0x800) {
                _bytes.push_front(0x80 | ((c      ) & 0x3F));
                _bytes.push_front(0xC0 | ((c >>  6)       ));
            } else if (c < 0x10000) {
                _bytes.push_front(0x80 | ((c      ) & 0x3F));
                _bytes.push_front(0x80 | ((c >>  6) & 0x3F));
                _bytes.push_front(0xC0 | ((c >> 12)       ));
            } else {
                _bytes.push_back(0x80 | ((c      ) & 0x3F));
                _bytes.push_back(0x80 | ((c >>  6) & 0x3F));
                _bytes.push_back(0x80 | ((c >> 12) & 0x3F));
                _bytes.push_back(0xC0 | ((c >> 18)       ));
            }
            _bytes.pop_back();
        }

        uint pop_back() {
            assert(!empty());
            iterator e = end();
            --e;
            uint c = *e;
            // this is a good argument for const_vector_view having _end rather than
            // _size
            // _bytes._size = (e._ptr - _bytes._begin);
            _bytes._end = const_cast<uchar*>(e._ptr); // <-- fixme
            // _bytes._begin[_bytes._size] = 0;
            *(_bytes._end) = 0;
            return c;
        }
        
        uint pop_front() {
            assert(!empty());
            iterator b = begin();
            uint c = *b;
            ++b;
            //_bytes._size -= (b._ptr - _bytes._begin);
            //_bytes._begin += (b._ptr - _bytes._begin);
            _bytes._begin = const_cast<uchar*>(b._ptr);
            return c;
        }
        
        bool empty() const {
            return _bytes.empty();
        }
        
        void clear() {
            _bytes.clear();
            _bytes.push_back(0);
            _bytes.pop_back();
        }
        
        void append(const char* z) {
            auto n = strlen(z);
            _bytes.append((uchar const*) z, (uchar const*) z + n + 1);
            _bytes.pop_back();
        }
        
        void append(string const& s) {
            _bytes.append(s._bytes.begin(), s._bytes.end() + 1);
            _bytes.pop_back();
        }
        
         bool operator==(const string& other) const {
             return std::equal(begin(), end(), other.begin(), other.end());
         }

        bool operator==(const string_view& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

    };
    
    inline string operator+(string_view a, char const* b) {
        string s(a);
        s.append(b);
        return s;
    }
    
    inline std::ostream& operator<<(std::ostream& a, string const& b) {
        a.write((char const*) b._bytes.begin(), b._bytes.size());
        return a;
    }
    
    template<typename Deserializer>
    inline auto deserialize(placeholder<string>, Deserializer& d) {
        return string{deserialize<array<uchar>>(d)};
    }
    
    
    struct immutable_string {
        
        // When a string is used as a hash table key, it is important to
        // keep the inline size small.  It will only be accessed for strcmp
        // to check hash collisions, and never mutated.  We can dispense
        // with _data and _capacity, place _end at the beginning of the
        // allocation, place the string after it, and infer _begin from
        // the allocation.
        
        // This is the simplest of several ways we can lay out the data, allowing
        // increasing mutability.  The downside is that accessing the iterators
        // requires pointer chasing, making use cases where we access the metadata
        // but not the data slower.
        
        // We don't store the hash since the hash table will usually do this
        
        struct implementation {
            
            uchar* _end;
            uchar _begin[];
            
            static implementation* make(string_view v) {
                auto n = v.as_bytes().size();
                implementation* p = (implementation*) malloc(sizeof(implementation) + n + 1);
                p->_end = p->_begin + n;
                std::memcpy(p->_begin, v.as_bytes().begin(), n);
                *p->_end = 0;
                return p;
            }
            
            static implementation* make(implementation* q) {
                auto n = q->_end - q->_begin;
                implementation* p = (implementation*) malloc(sizeof(implementation) + n + 1);
                p->_end = p->_begin + n;
                std::memcpy(p->_begin, q->_begin, n + 1);
                return p;
            }
            
        };
        
        implementation* _body;
        
        immutable_string() : _body(nullptr) {}
        
        immutable_string(immutable_string const&) = delete;
        
        immutable_string(immutable_string&& s)
        : _body(std::exchange(s._body, nullptr)) {}
        
        ~immutable_string() {
            free(_body);
        }
        
        immutable_string& operator=(immutable_string&& s) {
            immutable_string r(std::move(s));
            std::swap(_body, r._body);
            return *this;
        }
        
        immutable_string clone() const {
            immutable_string r;
            if (_body)
                r._body = implementation::make(_body);
            return r;
        }
        
        explicit immutable_string(string_view v)
        : _body(implementation::make(v)) {
        }
        
        using const_iterator = utf8_iterator;
        using iterator = utf8_iterator;
        
        const_iterator begin() const {
            return iterator{_body ? _body->_begin : nullptr};
        }
        
        const_iterator end() const {
            return iterator{_body ? _body->_end : nullptr};
        }
        
        char const* c_str() const {
            return _body ? (char const*) _body->_begin : nullptr;
        }
        
        uchar const* data() const {
            return _body ? (uchar const*) _body->_begin : nullptr;
        }
        
        array_view<const uchar> as_bytes() const {
            return array_view<const uchar>(_body ? reinterpret_cast<const uchar*>(_body->_begin) : nullptr,
                                          _body ? reinterpret_cast<const uchar*>(_body->_end + 1) : nullptr);
        }
        
        operator string_view() const {
            return string_view(begin(), end());
        }
        
        bool empty() const {
            return !(_body && (_body->_end - _body->_begin));
        }
        
        
    };
    
    string _string_from_file(string_view);
    
} // namespace manic




#endif /* string_hpp */
