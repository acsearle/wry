//
//  entity.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef entity_hpp
#define entity_hpp

#include "sim.hpp"
#include "term.hpp"

namespace wry {

    // Entity is the base class of game objects with behavior.  It IS-A
    // HeapTerm: an Entity reference can travel as the OBJECT payload of
    // a Term, participating in the wider persistent-data-structure /
    // save-load ecosystem.  Identity is carried by EntityID (see
    // entity_id.hpp); successive snapshots of the same entity are
    // distinct Entity allocations sharing an EntityID.
    //
    // Operator hooks (_term_add / _term_call / _term_eq / ...) are
    // inherited from HeapTerm with ERROR-returning defaults.  Entity
    // is identity-only by default: _term_eq returns false (no
    // content-equality), _term_less and _term_hash return ERROR.
    // Subclasses (Machine, ...) override what makes sense for them.

    struct Saver;
    struct Loader;

    struct Entity : HeapTerm {

        virtual ~Entity() = default;

        virtual void notify(TransactionContext*) const {}

        EntityID _entity_id;

        Entity();

        // _save_type_tag and _save_body are inherited as pure virtuals
        // from HeapTerm; concrete subclasses (Machine, ...) override.

    }; // struct Entity

};

#endif /* entity_hpp */
