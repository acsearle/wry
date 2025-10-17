//
//  entity.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "entity.hpp"

namespace wry {
    
    // It's not obvious how to let transactions create new unique EntityIDs
    // in a way that is consistent across different machines and different
    // thread interleavings.  The implementation below is not consistent, but
    // it is at least unique.
    //
    // If we propose new EntityIDs that are some hash of time and transaction
    // then we have to handle rare collisions.  Can we use placeholders until
    // the transactions are resolved, and serially replace them?
    // What about max id + parent's id?  This explodes.  
    //
    // Do they need to be consistent across machines, or just unique?  If they
    // aren't unique, we end up needing to get consistent unique priorities
    // somehow, etc.

    static constinit Atomic<uint64_t> _entity_id_oracle_state{0};

    EntityID EntityID::oracle() {
        return EntityID{_entity_id_oracle_state.add_fetch(1, Ordering::RELAXED)};
    }
    
    Entity::Entity() {
        _entity_id = EntityID::oracle();
    }

} // namespace wry::sim
