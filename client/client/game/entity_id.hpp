//
//  entity_id.hpp
//  client
//
//  Created by Antony Searle on 15/10/2025.
//

#ifndef entity_id_hpp
#define entity_id_hpp

#include "stdint.hpp"
#include "hash.hpp"

namespace wry {
    
    struct EntityID {
        uint64_t data;
        constexpr bool operator==(const EntityID&) const = default;
        constexpr auto operator<=>(const EntityID&) const = default;
        constexpr explicit operator bool() const { return (bool)data; }
        
        // oracle() is a placeholder. Once multiplayer is real, new IDs must be
        // assigned by a rule identical on every client, not merely unique:
        // priority = hash(EntityID) drives conflict resolution, so any
        // divergence in assignment desyncs the sim. Workable plan: a
        // transaction needing N new IDs registers N into a priority-ordered
        // structure during the tick; after the freeze barrier its base is
        // start_base + the prefix sum of higher-priority requesters' counts,
        // with start_base advancing by the tick total. The [base, base+N)
        // ranges partition the block (unique), and a prefix sum over the fixed
        // priority order with an associative op is independent of thread
        // schedule and machine (deterministic). New-entity references in a
        // transaction's proposed writes are stated relative to base and
        // resolved in an assign sub-phase between the barrier and resolution.
        // IDs burned by aborting transactions are acceptable holes (see the
        // compaction note below; that remap must also be deterministic).
        static EntityID oracle();
        // IDs are never reused, setting a hard scaling limit
        // TODO: We can compact IDs and times when loading, reducing this limit
        // to a per-session limit

        // The oracle restarts at zero each process, but loaded entities
        // carry their saved IDs; the loader calls this for every loaded ID
        // so that post-load spawns cannot collide with them.
        static void oracle_advance_past(EntityID);
    };
    
    inline u64 hash(const EntityID& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    template<typename> struct DefaultKeyService;
    
    
    template<>
    struct DefaultKeyService<EntityID> {
        using key_type = EntityID;
        using code_type = uint64_t;

        constexpr code_type encode(key_type key) const {
            return key.data;
        }

        constexpr key_type decode(code_type h) const {
            return EntityID{.data = h};
        }

        constexpr bool operator()(key_type a, key_type b) const {
            return encode(a) < encode(b);
        }

    
    };
    
    
    inline void garbage_collected_scan(const EntityID&) {}
    inline void garbage_collected_shade(const EntityID&) {}
    
} // namespace wry

#endif /* entity_id_hpp */
