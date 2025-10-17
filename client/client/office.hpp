//
//  office.hpp
//  client
//
//  Created by Antony Searle on 11/10/2025.
//

#ifndef office_hpp
#define office_hpp

#include <vector>

#include "algorithm.hpp"
#include "mutex.hpp"
#include "stdint.hpp"

namespace wry {
    
    // Problem statement:
    //
    // Concurrently, a number of transactions are proposed
    //     Each has a priority
    //     Each has an identity (the EntityID of its creator?)
    //     Each may request N unique EntityIDs
    //
    // Barrier
    //
    // Concurrently, transactions are resolved and their effects are applied
    //     A transaction may abort and not need any EntityIDs
    //     A transaction may commit and will need its EntityIDs
    //     The new EntityIDs are written to the world state
    //     The EntityIDs allocated must be deterministic
    
    // The production of EntityIDs on demand would be trivial without the
    // requirement that different executions produce the same results; we could
    // just use an atomic counter.
    
    // A straightforward implementation is to sort requests by priority, then
    // use the cumulative sum of requests that are made by committed
    // transactions, plus some starting ID, to produce deterministic and dense
    // IDs
    //
    // The downside of the above scheme is that labelling the transactions in
    // order is linear in the number of transactions (that request EntityIDs).
    // This is a classic Amdahl bottleneck of serial work.
    //
    // TODO: "list-labeling" is a classic problem, look it up
    //
    // We can shard priority and ID space, to convert this O(N) dependency chain
    // into M independent O(N/M) dependency chains; so long as M >= threads this
    // is about as good as possible?
    //
    //
    //
    //
    // Our skiplists are insert-only.  Can we maintain something that allows
    // them to be ranked once they are frozen?
    //
    // Priorities are inserted into the list.
    // Let each pointer be associated with a count of the number of leafs
    // skipped over by that pointer.  (This is inconsistent until the skiplist
    // is frozen)
    //
    // When we insert an element, we walk up the chain adding one to things?
    // We can't do that actually, because when we insert at some level we split
    // the count, but we can't do so atomically
    //
    // So, wait until the skiplist is frozen.
    //
    // We can accumulate the skipcounts for each level by parallel recursive
    // descent, with log N longest path and total cost N.  And we can then
    // compute the rank of an element in log N, same as finding it, by
    // accumulating on our path to it.
    //
    // It would cost N to process each node in turn, we have made it cost
    // more like N log N ?
    //
    //
    //
    // All of these ideas depend on having a way to know when the progressive
    // counting (and the count) is finalized, as compared to the idempotent
    // transaction resolution.  Time to embrace coroutines?
    
    // The EntityID allocation is an example of a transaction target that is
    // neither exclusive nor commutative; multiple tx interact with it and
    // their resultant states depend on each other.  This is what we strive
    // to prevent with transactions in the first place.
    //
    // We can relax this somewhat by making the allocation independent of
    // if the transactions commit (i.e. wasting the aborted IDs), but we have
    // to know how many participate else we can't avoid collisions (a UUID
    // scheme seems to inevitably collide)
    //
    // We could allow EntityIDs to differ between system and translate them
    // somehow, but then we simply push the problem to choosing consistent
    // priorities for the entity IDs.
    //
    // We could remove the notion of identity and deal with entities by
    // location and type or something?  Seems ineviatble we would need to
    // filter by some ID in that scheme.
    
    // We can restrict the number of new entities created each tick (perhaps to
    // only one!), making the EntityID value a normal exclusive thingy.
    //
    // We can shard EntityID oracles, making many new Entities per tick
    // possible, but not guaranteed.  This rate limit seems a bit unphysical
    // though.
    //
    // EntityID as a function of location and timestamp is not unique enough and
    // squanders ID space, unless we hash, and then we collide.
    
    
    
    

    // An office issues tickets
    
    struct BlockingOfficeState {
        
        FastBasicLockable mutex;
        uint64_t count = 0;
        std::vector<uint64_t> priorities;
        bool processed = false;
        
        void open() {
            
        };
        
        void apply(uint64_t priority) {
            std::unique_lock lock{mutex};
            assert(!processed);
            priorities.push_back(priority);
        };
        
        void _assign() {
            std::sort(priorities.begin(), priorities.end());
            auto partition = std::unique(priorities.begin(), priorities.end());
            priorities.erase(partition, priorities.end());
        }
        
        uint64_t collect(uint64_t priority) {
            std::unique_lock lock{mutex};
            if (!processed) {
                _assign();
                processed = true;
            }
            auto a = std::lower_bound(priorities.begin(), priorities.end(), priority);
            assert(a != priorities.end());
            assert(*a == priority);
            return count + std::distance(priorities.begin(), a);
        };
        
        void close() {
            std::unique_lock lock{mutex};
            count += priorities.size();
            priorities.clear();
        }
        
    }; // BlockingOfficeState
    
} // namespace wry


#endif /* office_hpp */
