//
//  command_types.hpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//
//  Command literal types for the game's vocabulary (core/docs/commands.md
//  section 4.4):
//
//      Coordinate(12, -34)
//      EntityID(76474647846)
//      OPCODE::SWAP            (bare SWAP where an OPCODE is expected)
//      MATTER::SHIPPING_CONTAINER
//      Term: null, true, false, integers, "strings", OPCODE::..., MATTER::...,
//            EntityID(...)
//      Hash(0x9e3779b97f4a7c15)
//
//  A Term parameter has no expected type, so its literals must describe
//  themselves: enumerators are always qualified there.
//

#ifndef command_types_hpp
#define command_types_hpp

#include "command.hpp"
#include "coordinate.hpp"
#include "entity_id.hpp"
#include "matter.hpp"
#include "opcode.hpp"
#include "term.hpp"

namespace wry {

    // A 64-bit content hash (core/hash.hpp): the reply of `state-hash`,
    // the World hash lockstep peers compare.  Printed in full-width hex so
    // two replies line up for the eye.
    struct Hash {
        uint64_t value;
        constexpr bool operator==(const Hash&) const = default;
    };

} // namespace wry

namespace wry::command {

    template<> struct Type<Hash> {
        static constexpr const char* name = "Hash";
        static constexpr const char* example = "Hash(0x9e3779b97f4a7c15)";
        static std::expected<Hash, Error> read(Node const& node);
        static void print(String& out, Hash const& x);
    };

    template<> struct Type<Coordinate> {
        static constexpr const char* name = "Coordinate";
        static constexpr const char* example = "Coordinate(12, -34)";
        static std::expected<Coordinate, Error> read(Node const& node);
        static void print(String& out, Coordinate const& x);
    };

    template<> struct Type<EntityID> {
        static constexpr const char* name = "EntityID";
        static constexpr const char* example = "EntityID(7)";
        static std::expected<EntityID, Error> read(Node const& node);
        static void print(String& out, EntityID const& x);
    };

    // Enumerators are spelled TYPE::NAME, where the C++ enumerator is
    // TYPE_NAME (OPCODE_SWAP -> OPCODE::SWAP).

    template<> struct Type<OPCODE> {
        static constexpr const char* name = "OPCODE";
        static constexpr const char* example = "OPCODE::SWAP (or bare SWAP)";
        static std::expected<OPCODE, Error> read(Node const& node);
        static void print(String& out, OPCODE const& x);
    };

    template<> struct Type<MATTER> {
        static constexpr const char* name = "MATTER";
        static constexpr const char* example = "MATTER::SHIPPING_CONTAINER";
        static std::expected<MATTER, Error> read(Node const& node);
        static void print(String& out, MATTER const& x);
    };

    // Term arguments are rooted while bound (commands.md section 6: the
    // only GC-visible state is an invocation in flight).
    //
    // Printable Terms: null, booleans, integers (small and heap), strings
    // (short and heap) that are valid UTF-8, opcodes, matter, entity ids.
    // Anything else -- characters, errors, sentinels, other heap objects
    // -- has no literal, so cannot be an argument.
    template<> struct Type<Term> {
        static constexpr const char* name = "Term";
        static constexpr const char* example = "null, true, 42, \"text\", OPCODE::SWAP, EntityID(7)";
        using stored_type = Root<Term>;
        static Term const& unstore(Root<Term> const& x) { return x._value; }
        static std::expected<Term, Error> read(Node const& node);
        static void print(String& out, Term const& x);
        static bool printable(Term const& x);
    };

} // namespace wry::command

#endif /* command_types_hpp */
