//
//  entity.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "entity.hpp"

namespace wry {
    
    // When new entities are spawned, they need consistent identifiers across
    // different computers.  It's not obvious how to do this elegantly.
    // - Serially resolve in priority order?
    // - Shard the above into independent parts?
    
    // This placeholder implementation is unique but nondeterministic and
    // thus unsuitable for multiplayer
    
    static constinit Atomic<uint64_t> _entity_id_oracle_state{0};

    EntityID EntityID::oracle() {
        return EntityID{_entity_id_oracle_state.add_fetch_relaxed(1)};
    }

    void EntityID::oracle_advance_past(EntityID id) {
        // Atomic max; relaxed suffices because uniqueness needs only the
        // per-location RMW total order, not cross-location ordering.
        _entity_id_oracle_state.fetch_max_relaxed(id.data);
    }
    
    Entity::Entity() {
        _entity_id = EntityID::oracle();
    }

} // namespace wry::sim
