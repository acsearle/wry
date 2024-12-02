//
//  entity.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "entity.hpp"

namespace wry::sim {

    uint64_t entity_get_priority(const Entity* entity) {
        return entity->_entity_id.data;
    }

} // namespace wry::sim
