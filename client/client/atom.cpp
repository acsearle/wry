//
//  atom.cpp
//  client
//
//  Created by Antony Searle on 25/1/2024.
//

#include <mutex>
#include <map>

#include "atom.hpp"
#include "test.hpp"

namespace wry::atom {
    
    namespace {
         
        // lexicographical comparison of null terminated strings
        struct Compare {
            using is_transparent = void;
            bool operator()(const char* s1, const char* s2) const {
                return strlt(s1, s2);
            }
            bool operator()(const char* s1, StringView s2) const {
                for (;;) {
                    if (s2.empty())
                        return false;
                    if (!*s1 || (*s1 != s2.chars.front()))
                        break;
                    ++s1;
                    s2.pop_front();
                }
                return ((unsigned char) *s1) <  (unsigned char) s2.chars.front();
            }
            bool operator()(StringView s1, const char* s2) const {
                for (;;) {
                    if (s1.empty())
                        return *s2;
                    if (!*s2 || (s1.chars.front() != *s2))
                        break;
                    s1.pop_front();
                    ++s2;
                }
                return ((unsigned char) s1.chars.front()) < ((unsigned char) *s2);
            }
        };
        
        // generates full period pseudorandom sequence
        std::uint64_t xorshift64(std::uint64_t x) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        }

        // TODO: _string_to_atom should be some sort of prefix tree
        
        std::mutex _string_to_atom_mutex;
        Atom next = Atom{0xc864372cd8fb4734};
        std::map<const char*, Atom, Compare> _string_to_atom;
        
        std::mutex _atom_to_string_mutex;
        AtomMap<const char*> _atom_to_string;
        
        auto _from_string_helper(const char* s) {
            auto [i, f] = _string_to_atom.insert({s, next});
            assert(f);
            {
                auto guard2 = std::unique_lock(_atom_to_string_mutex);
                _atom_to_string.emplace(next, s);
            }
            next.data = xorshift64(next.data);
            return i;
        }
        
    }
    
    // To get the atom for a string, we lock the table and look it up.  If it
    // exists, we are done.  If it does not exist, we use the next value from
    // a pseudorandom sequence, insert it into both the _string_to_atom and
    // _atom_to_string maps, advance the pseudorandom sequence, and return
    
    // We expect these operations to be mostly used in, respectively,
    // deserialization and serialization.
        
    Atom Atom::from_string(const char* s) {
        auto guard = std::unique_lock(_string_to_atom_mutex);
        auto i = _string_to_atom.find(s);
        if (i == _string_to_atom.end()) {
            i = _from_string_helper(s);
        }
        return i->second;
    }

    Atom Atom::from_string(StringView s) {
        auto guard = std::unique_lock(_string_to_atom_mutex);
        auto i = _string_to_atom.find(s);
        if (i == _string_to_atom.end()) {
            char* t = (char*) operator new(s.chars.size() + 1);
            std::memcpy(t, s.chars.data(), s.chars.size());
            t[s.chars.size()] = 0;
            _from_string_helper(t);
        }
        return i->second;
    }
    const char* Atom::to_string() const {
        auto guard = std::unique_lock(_atom_to_string_mutex);
        return _atom_to_string[*this];
    }
    
} // namespace wry::atom

namespace wry {
    
    define_test("atom") {
        
        using atom::Atom;
        
        auto a = Atom::from_string("a");
        auto b = Atom::from_string("b");
        auto c = Atom::from_string("a");
        
        assert(a != b);
        assert(a == c);
        assert(b != c);
        
        assert(!strcmp(a.to_string(), "a"));
        assert(strcmp(a.to_string(), "b"));
        assert(!strcmp(b.to_string(), "b"));
        assert(strcmp(b.to_string(), "c"));
        assert(!strcmp(c.to_string(), "a"));
        assert(strcmp(c.to_string(), "d"));
        
        auto d = Atom::from_string("aa");
        assert(d != a);
        assert(!strcmp(d.to_string(), "aa"));
        assert(strcmp(d.to_string(), "a"));
                
    };
    
}
