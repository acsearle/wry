//
//  save_types.hpp
//  client
//
//  Stable type tags for the save format, and the central X-macro list of
//  saveable concrete types that drives the loader's factory table.
//

#ifndef save_types_hpp
#define save_types_hpp

#include <string_view>

#include "stdint.hpp"

namespace wry {

    // FNV-1a-64 over a string literal.  Used to derive a stable type tag from
    // a class's fully-qualified name.  Hash collision across the ~50 types we
    // expect to register is statistically inconceivable.
    //
    // If a class is renamed but should keep loading old saves, give it an
    // explicit string literal here that matches the old name.

    constexpr uint64_t save_type_tag_fnv1a(std::string_view s) {
        uint64_t h = 0xcbf29ce484222325ull;
        for (char c : s) {
            h ^= (uint8_t)c;
            h *= 0x100000001b3ull;
        }
        return h;
    }

    // Helper for structural tags on template instantiations.  Combines a
    // base-name tag with sub-type tags via odd rotations, so distinct (T, U)
    // pairs produce distinct outer tags.

    constexpr uint64_t save_type_tag_combine(uint64_t base, uint64_t sub, int shift) {
        return base ^ ((sub << shift) | (sub >> (64 - shift)));
    }

    // Primary template; specialize per type to give it a save tag.
    // For concrete classes, prefer `static constexpr uint64_t SAVE_TYPE_TAG`
    // on the class itself, and a one-liner specialization here that forwards
    // to it.  For template instantiations (AMT Node<T, U>, etc.) specialize
    // here with a structural combine.

    template<typename T>
    struct save_type_traits;  // intentionally undefined

    template<typename T>
    inline constexpr uint64_t save_type_tag_v = save_type_traits<T>::value;

}  // namespace wry

#endif  // save_types_hpp
