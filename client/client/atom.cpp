//
//  atom.cpp
//  client
//
//  Created by Antony Searle on 25/1/2024.
//

#include <mutex>
#include <map>

#include "cstring.hpp"
#include "atom.hpp"
#include "test.hpp"

namespace wry::atom {
    
    namespace {
         
        // lexicographical comparison of null terminated strings
        //
        // TODO: What about operator()(StringView a, StringView b) { return a < b; }
        // it will double-walk the memory but does it matter?
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
        
        // Basic 64-bit xorshift
        
        constexpr uint64_t xorshift64(uint64_t x) {
            assert(x);
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        }

        // Basic xorshift steps
        
        constexpr std::uint64_t lstep(std::uint64_t x, int n) {
            return x ^ (x << n);
        }

        constexpr std::uint64_t rstep(std::uint64_t x, int n) {
            return x ^ (x >> n);
        }

        // Inverted xorshift steps
        //
        // For
        //
        //     y = x ^ (x << n)
        //
        // the low n bits of y are just x.  A second application of the forward
        // step recovers another n bits of x.
        //
        //     y ^ (y << n) = (x ^ (x << n)) ^ ((x ^ (x << n)) << n)
        //                  = x ^ (x << n) ^ (x << n) ^ (x << 2n)
        //                  = x ^ (x << 2n)
        //
        // We can thus apply n, 2n, 4n... until n >= 64 and we recover all the
        // bits of x
        
        // todo: carryless multiply instructions?
                
        constexpr std::uint64_t ilstep(std::uint64_t x, int n) {
            assert(x && (n > 0));
            [[maybe_unused]] auto y = x;
            [[maybe_unused]] auto m = n;
            while (n & 63) {
                x ^= x << n;
                n <<= 1;
            }
            assert((x ^ (x << m)) == y);
            return x;
        }

        // inverse of x ^= x >> n
        constexpr std::uint64_t irstep(std::uint64_t x, int n) {
            assert(x && (n > 0));
            [[maybe_unused]] auto y = x;
            [[maybe_unused]] auto m = n;
            while (n & 63) {
                x ^= x >> n;
                n <<= 1;
            }
            assert((x ^ (x >> m)) == y);
            return x;
        }
        
        constexpr std::uint64_t ixorshift64(std::uint64_t x) {
            [[maybe_unused]] std::uint64_t y = x;
            x = ilstep(x, 17);
            x = irstep(x, 7);
            x = ilstep(x, 13);
            assert(y == xorshift64(x));
            return x;
            
        }

        // TODO: _string_to_atom should be some sort of prefix tree or a
        // conventional hash map
        //
        // in a binary tree we are doing doing O(M) comparisons O(log N) times
        
        std::mutex _string_to_atom_mutex;
        Atom next = Atom{0xc864372cd8fb4734};  // high entropy seed
        std::map<const char*, Atom, Compare> _string_to_atom;
        
        std::mutex _atom_to_string_mutex;
        AtomMap<const char*> _atom_to_string;
        
        auto _from_string_helper(const char* s) {
            auto [i, f] = _string_to_atom.insert({s, next});
            assert(f);
            {
                auto guard2 = std::unique_lock(_atom_to_string_mutex);
                _atom_to_string.try_emplace(next, s);
            }
            next.data = xorshift64(next.data);
            return i;
        }
        
    }    
    // To get the atom for a string, we lock the table and look it up.  If it
    // exists, we are done.  If it does not exist, we use the next value from
    // a pseudorandom sequence, insert it into both the _string_to_atom and
    // _atom_to_string maps, advance the pseudorandom sequence, and return.
    
    // We use a pseudorandom sequence because we need the atom bits to be a
    // good hash value with every bit unpredictable; for example, using a
    // counter would result in all the top bits of the has code being zero
    
    // We expect these operations to be mostly used in, respectively,
    // deserialization and serialization.  When the app is running normally,
    // it will just compare atoms directly without needing strings.
        
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
