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

        EntityID _entity_id;
        mutable EntityID _free_entity_id;

        // _save_type_tag and _save_body are inherited as pure virtuals
        // from HeapTerm; concrete subclasses (Machine, ...) override.

        // The ticket is the committer's late write, made just before it
        // seals the entity (World::step's entity-map action), so it is in
        // the hash like any other state.
        template<typename V>
        void _fields(V& v) const {
            v.state("_entity_id", _entity_id);
            v.state("_free_entity_id", _free_entity_id);
        }

        [[nodiscard]] virtual int64_t notify(TransactionContext*) const { return 0; }
        [[nodiscard]] virtual Entity* make_mutable_clone() const = 0;


    }; // struct Entity

    // The value hash of an entity map entry: the entity's sealed content
    // hash (HeapTerm::content_hash), zero for no entity.
    inline uint64_t content_hash(const Entity* _Nullable p) {
        return p ? p->content_hash() : 0;
    }

};

#endif /* entity_hpp */
