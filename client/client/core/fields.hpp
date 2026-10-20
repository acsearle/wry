//
//  fields.hpp
//  client
//
//  Created by Antony Searle on 20/10/2026.
//
//  One enumeration of a class's fields, many consumers.
//
//  A record-like garbage collected object (an Entity, a HeapTerm value,
//  the World) declares
//
//      template<typename V> void _fields(V& v) const {
//          Base::_fields(v);
//          v.state("_stack", _stack);
//          v.transient("_queue", _queue);
//      }
//
//  and the collector's scan, the debug printer, the save body and the
//  content hash are visitors over it (the latter two in io/save_format
//  and game/term.hpp).  Dispatch is static: each visitor is a type, and
//  _fields is a template instantiated once per visitor.  The collector
//  and the debug printer need a virtual entry point because they hold a
//  GarbageCollected*, so the Fields mixin (term.hpp) wraps the
//  instantiations in the three virtuals; the save and the hash callers
//  know the concrete type or go through the same virtuals.
//
//  Roles say which consumers see a field:
//
//      state      simulation state: scanned, printed, saved, hashed
//      transient  scanned and printed only: process-local (Player's
//                 input queue; the World's _ready, whose saved form is
//                 the fold into the time wheel)
//      derived    printed only: a cache of other fields (HeapString's
//                 interning hash)
//
//  A field's per-visitor treatment is found by argument-dependent lookup
//  on its type (garbage_collected_scan, content_hash, ...), as the
//  collector's scan has always been written, so a new field type needs
//  an overload beside the type, not a change here.
//
//  No consistency check yet: a field left out of _fields is invisible to
//  all four consumers.  A tool reading the class members from clang's
//  AST and comparing them to _fields is the planned enforcement.
//

#ifndef fields_hpp
#define fields_hpp

#include <cstdio>
#include <string_view>
#include <type_traits>

#include "garbage_collected.hpp"
#include "hash.hpp"
#include "stdint.hpp"

namespace wry {

    enum class FieldRole : uint8_t {
        state,
        transient,
        derived,
    };

    // The visitor helper: a visitor derives from FieldVisitor<Self> and
    // defines
    //
    //     template<FieldRole R, typename T> void visit(const char* name, T const& x);
    //
    // receiving every field with its role as a template argument, so a
    // visitor that ignores a role never instantiates anything for the
    // field (a transient input queue has no content hash, and need not).
    template<typename Self>
    struct FieldVisitor {
        Self& self() { return static_cast<Self&>(*this); }
        template<typename T> void state(const char* name, T const& x)     { self().template visit<FieldRole::state>(name, x); }
        template<typename T> void transient(const char* name, T const& x) { self().template visit<FieldRole::transient>(name, x); }
        template<typename T> void derived(const char* name, T const& x)   { self().template visit<FieldRole::derived>(name, x); }
    };

    // Scan no-ops for field types that hold no pointers and have no
    // namespace of their own for argument-dependent lookup to find.
    template<typename T> requires std::is_enum_v<T>
    void garbage_collected_scan(T) {}
    inline void garbage_collected_scan(std::string_view) {}

    // The collector's scan: every field, whatever its role (a transient
    // input queue still holds Terms).
    struct ScanFieldVisitor : FieldVisitor<ScanFieldVisitor> {
        template<FieldRole R, typename T>
        void visit(const char*, T const& x) {
            garbage_collected_scan(x);
        }
    };

    // The debug printer: name and value of every field, one per line,
    // for the handful of types a printf can show; the rest by name.
    struct DebugFieldVisitor : FieldVisitor<DebugFieldVisitor> {
        template<FieldRole R, typename T>
        void visit(const char* name, T const& x) {
            if constexpr (std::is_enum_v<T>) {
                printf("    %s = %lld\n", name, (long long)x);
            } else if constexpr (std::is_integral_v<T>) {
                printf("    %s = %lld\n", name, (long long)x);
            } else if constexpr (std::is_pointer_v<T>) {
                printf("    %s = %p\n", name, (const void*)x);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                printf("    %s = \"%.*s\"\n", name, (int)x.size(), x.data());
            } else if constexpr (requires { content_hash(x); }) {
                printf("    %s : content hash %016llx\n", name, (unsigned long long)content_hash(x));
            } else {
                printf("    %s\n", name);
            }
        }
    };

    // The content hash: the type tag, then each state field's content
    // hash (core/hash.hpp), combined positionally.  Transient and
    // derived fields do not take part.
    struct HashFieldVisitor : FieldVisitor<HashFieldVisitor> {
        CombineHasher _hasher;
        explicit HashFieldVisitor(uint64_t type_tag) {
            _hasher.write_u64(type_tag);
        }
        template<FieldRole R, typename T>
        void visit(const char*, T const& x) {
            if constexpr (R == FieldRole::state)
                _hasher.write_u64(content_hash(x));
        }
        uint64_t finish() const {
            return _hasher.finish();
        }
    };

} // namespace wry

#endif /* fields_hpp */
