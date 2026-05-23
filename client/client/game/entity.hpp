//
//  entity.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef entity_hpp
#define entity_hpp

#include "sim.hpp"

namespace wry {
        
    // This is the base class of things with behavior
    //
    // They are GarbageCollected, but are they Values?
        
    struct Saver;
    struct Loader;

    struct Entity : GarbageCollected {

        virtual ~Entity() = default;

        virtual void notify(TransactionContext*) const {}

        EntityID _entity_id;

        Entity();

        // Save format dispatch.  Returns the SAVE_TYPE_TAG of the dynamic
        // type.  Concrete subclasses must override.
        virtual uint64_t _save_type_tag() const = 0;

        // Emit this entity's record body to the saver.  Visits children
        // (e.g. PersistentStack head) as part of body emission.
        virtual void _save_body(Saver& saver) const = 0;

    }; // struct Entity
      
};

#endif /* entity_hpp */
