//
//  entity.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef entity_hpp
#define entity_hpp

#include "sim.hpp"
#include "value.hpp"

namespace wry {

    // Entity is the base class of game objects with behavior.  It IS-A
    // HeapValue: an Entity reference can travel as the OBJECT payload of
    // a Value, participating in the wider persistent-data-structure /
    // save-load ecosystem.  Identity is carried by EntityID (see
    // entity_id.hpp); successive snapshots of the same entity are
    // distinct Entity allocations sharing an EntityID.
    //
    // Operator hooks (_value_add / _value_call / _value_eq / ...) are
    // inherited from HeapValue with ERROR-returning defaults.  Entity
    // is identity-only by default: _value_eq returns false (no
    // content-equality), _value_less and _value_hash return ERROR.
    // Subclasses (Machine, ...) override what makes sense for them.

    struct Saver;
    struct Loader;

    struct Entity : HeapValue {

        virtual ~Entity() = default;

        virtual void notify(TransactionContext*) const {}

        EntityID _entity_id;

        Entity();

        // _save_type_tag and _save_body are inherited as pure virtuals
        // from HeapValue; concrete subclasses (Machine, ...) override.

    }; // struct Entity

};

#endif /* entity_hpp */
