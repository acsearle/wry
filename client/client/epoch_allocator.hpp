//
//  epoch_allocator.hpp
//  client
//
//  Created by Antony Searle on 13/10/2025.
//

#ifndef epoch_allocator_hpp
#define epoch_allocator_hpp

#include "bump_allocator.hpp"
#include "epoch_service.hpp"

namespace wry {
    
    namespace epoch {
        
        // Threads cooperatively advance the epoch by calling
        // epoch::pin(), epoch::unpin() and epoch::repin()
        
        // An EpochAllocated object will survive at least as long as the
        // current Epoch, and the current Epoch will endure at least as long
        // as any thread is pinned.
        
        // We pin each frame, and each worker.
        
        // TODO: If a thread takes multiple pins, we can't know how many
        // are in prior or in current.
        // - Only allow one per thread?
        // - Count per thread and only have one actual?
        // - Pins yield a token that unpins consume?
        //   - This has the advantage of decoupling pins from threads
        
        // Instantitate a singleton epoch service for the epoch allocator
        inline static constinit Service allocator_global_service = {};
        
        struct LocalState {
            bump::Slab* _Nullable bump_alternate;
            Epoch known;
            
            // The epoch allocator wraps the bump allocator with management
            // that
            void _update_with(Epoch observed) {
                if (observed != known) {
                    bump_alternate = bump::this_thread_state.exchange_head_and_restart(bump_alternate);
                    known = observed;
                }
            }
            
            void pin() {
                _update_with(allocator_global_service.pin());
            }
            
            void unpin() {
                _update_with(allocator_global_service.unpin(known));
            }
            
            void repin() {
                _update_with(allocator_global_service.repin(known));
            }
            
        };
        
        inline constinit thread_local LocalState allocator_local_state = {};
        
        inline void pin() {
            allocator_local_state.pin();
        }
        
        inline void unpin() {
            allocator_local_state.unpin();
        }
        
        inline void repin() {
            allocator_local_state.repin();
        }
        
    } // nampespace wry::epoch
    
    using EpochAllocated = BumpAllocated;
    
} // namespace wry
#endif /* epoch_allocator_hpp */
