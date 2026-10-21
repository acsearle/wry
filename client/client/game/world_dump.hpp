//
//  world_dump.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  A World's state as text, pointer-free and in a content-determined
//  order, so that two Worlds with the same content dump identically and
//  a line diff of two dumps names what differs: the structural diff of
//  the desync diagnosis (game/docs/multiplayer.md section 5), reached
//  through the `dump` command.
//
//  Records are the objects the save walks (the World, its entities, its
//  heap values), numbered #1 upward in first-reference order from the
//  World, each printed as its type and its state fields -- the _fields
//  enumeration of core/fields.hpp, so a class's fields appear here with
//  no code of its own.  A reference prints as the record number; a map
//  as its entries in code order, one per line; a set and a stack
//  inline.
//

#ifndef world_dump_hpp
#define world_dump_hpp

#include <string>

#include "world.hpp"

namespace wry {

    std::string dump_world(World const* world);

} // namespace wry

#endif /* world_dump_hpp */
