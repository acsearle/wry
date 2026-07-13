//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string>
#include <string_view>

#include "term.hpp"
#include "ctrie.hpp"
#include "garbage_collected.hpp"

namespace wry {
    
    struct HeapString final : HeapTerm {

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::HeapString");

        static void* operator new(std::size_t, void* ptr) noexcept { return ptr; }

        static const HeapString* make(std::size_t hash, std::string_view view);
        static const HeapString* make(std::string_view view);

        size_t _hash;
        size_t _size;
        char _bytes[] __counted_by(_size);

        std::string_view as_string_view() const;

        HeapString();
        virtual ~HeapString() override final;

        virtual void _garbage_collected_scan() const override final;
        virtual void _garbage_collected_debug() const override final;

        // Content equality / ordering / hash.  Two HeapStrings with the
        // same bytes compare equal and hash equal even though they may
        // sit at different addresses (interning is unwired in production).
        virtual Term _term_eq(Term right) const override final;
        virtual Term _term_less(Term right) const override final;
        virtual Term _term_hash() const override final;

        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

    }; // struct HeapString

    inline size_t hash(HeapString const* a) {
        return a->_hash;
    }

//    template<size_t N> requires (N > 0)
//    constexpr Term::Term(const char (&ntbs)[N]) {
//        const size_t M = N - 1;
//        assert(ntbs[M] == '\0');
//        if (M < 8) {
//            _short_string_t s;
//            s._tag_and_len = (M << TERM_SHIFT) | TERM_TAG_SHORT_STRING;
//            // builtin for constexpr
//            __builtin_memcpy(s._chars, ntbs, M);
//            __builtin_memcpy(&_data, &s, 8);
//        } else {
//            _data = (uint64_t)HeapString::make(ntbs);
//        }
//    }




    // TODO: Where do these go?
    //
    // We can imagine genericising this to a memo system where we need
    //
    // memo for foo(Key) -> Term
    //
    // need Key, Term, foo, KeyHasher, KeyEqual
    //
    // WeakHolder has to be able to provide a key to erase
    //
    // In particular, we have a bunch of problems:
    //
    // WeakHolder holds HeapString, which is equivalent to Key
    // WeakHolder needs to have a Key to erase itself.
    //    If being erased by the collector, it can rely on the HeapString still being alive
    //    If being replaced by the mutator, the Key is not needed
    // SNode needs to hold a Key and a Term
    //    If being replaced by the mutator, this is the Key used.
    // LNodes lists hold SNodes all with the same hash, but different Keys
    // LNodes are rare but also expensive when they get hit.

    // Does the trie need to know anything about Keys and Values or is it a
    // structure mapping hash to a list of (Key, Term) pairs?

    template<typename> struct WeakHolder;
    void _heap_string_ctrie_collector_try_erase(WeakHolder<HeapString> const* victim);

    struct std_string_Hasher {
        std::size_t operator()(std::string const& x) const {
            return wry::hash_combine(x.data(), x.size());
        }
    };

    inline void garbage_collected_scan(std_string_Hasher const&) {}

    using WeakDict = Ctrie<std::string, WeakHolder<HeapString> const*, std_string_Hasher>;

    inline WeakDict* heap_string_weak_dictionary() {
        static Root<WeakDict*> _ct{new WeakDict};
        return _ct._ptr;
    }

    // `did_emplace`, when supplied, reports whether the call installed a
    // fresh string (true) or upgraded an existing holder (false).  The alter
    // lambda may run several times racing other mutators; the invocation
    // whose CAS wins runs last, so the flag ends with the effective path.
    inline HeapString const* _heap_string_ctrie_mutator_find_upgrade_or_emplace(std::string s,
                                                                                bool* did_emplace = nullptr) {
        WeakDict* t = heap_string_weak_dictionary();
        auto [before, after] = t->alter(s, [&s, did_emplace](std::optional<WeakHolder<HeapString> const*> before) {
            if (before.has_value()) {
                // It does exist, try to get permission:
                if (before.value()->mutator_try_upgrade()) {
                    printf("%p:HeapString: mutator upgraded\n", before.value()->_weak);
                    if (did_emplace)
                        *did_emplace = false;
                    return WeakDict::AlterChoice::keep();
                }
                printf("%p:HeapString: mutator upgrade failed\n", before.value()->_weak);
                // On this path, _weak is GONE and may already be invalid
            }
            // Compete to install a new string
            HeapString const* nhs = HeapString::make(s);
            printf("%p:HeapString: mutator tries to install\n", nhs);
            WeakHolder<HeapString> const* nwh = new WeakHolder<HeapString>{nhs};
            if (did_emplace)
                *did_emplace = true;
            return WeakDict::AlterChoice::replace(nwh);
        });
        return (*after)->_weak;
    }

    inline void _heap_string_ctrie_collector_try_erase(WeakHolder<HeapString> const* victim) {
        WeakDict* t = heap_string_weak_dictionary();
        // The collector is in charge of killing the string, so it knows it is
        // still alive, so we can get the key out of it
        std::string s{victim->_weak->as_string_view()};
        (void) t->alter(s, [victim, &s](std::optional<WeakHolder<HeapString> const*> before) {
            if (before.has_value() && before.value() == victim) {
                printf("HeapString: collector erases\n");
                return WeakDict::AlterChoice::erase();
            } else {
                printf("HeapString: collector found replacement\n");
                return WeakDict::AlterChoice::keep();
            }
        });
    }



} // namespace wry

#endif /* wry_gc_HeapString_hpp */
