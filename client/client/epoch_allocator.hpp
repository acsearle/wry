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
            bool is_pinned;
            
            // The epoch allocator wraps the bump allocator with management
            // that
            void _update_with(Epoch observed) {
                if (observed != known) {
                    bump_alternate = bump::this_thread_state.exchange_head_and_restart(bump_alternate);
                    known = observed;
                }
            }
            
            void pin() {
                assert(!is_pinned);
                _update_with(allocator_global_service.pin());
                is_pinned = true;
                
            }
            
            void unpin() {
                assert(is_pinned);
                _update_with(allocator_global_service.unpin(known));
                is_pinned = false;
            }
            
            void repin() {
                assert(is_pinned);
                _update_with(allocator_global_service.repin(known));
            }
            
        };
        
        inline constinit thread_local LocalState allocator_local_state = {};
        
        // Keep the epoch pinned while a thread is awake
        
        inline void pin_this_thread() {
            allocator_local_state.pin();
        }
        
        inline void unpin_this_thread() {
            allocator_local_state.unpin();
        }
        
        inline void repin_this_thread() {
            allocator_local_state.repin();
        }
        
        // Pin the local thread's known epoch again.  The returned epoch can
        // be used to unpin that epoch on a different thread.
        //
        // This is used to tie the epoch to a non-thread scope, such as the
        // lifetime of the root of a tree of jobs.
        
        [[nodiscard]] inline auto
        pin_explicit() -> Epoch {
            assert(allocator_local_state.is_pinned);
            return allocator_global_service.pin_explicit(allocator_local_state.known);
        }

        inline auto
        unpin_explicit(Epoch pinned) -> Epoch {
            assert(allocator_local_state.is_pinned);
            return allocator_global_service.unpin(pinned);
        }

        [[nodiscard]] inline auto
        repin_explicit(Epoch pinned) -> Epoch {
            assert(allocator_local_state.is_pinned);
            return allocator_global_service.repin_explicit(pinned);
        }

        inline void* _Nonnull allocate(size_t count) {
            assert(allocator_local_state.is_pinned);
            return bump::allocate(count);
        }
        
        inline void deallocate(void* _Nullable) {
            assert(allocator_local_state.is_pinned);
            // no op
        }
        
    } // nampespace wry::epoch
    
    using EpochAllocated = BumpAllocated;
    template<typename T> using EpochAllocator = BumpAllocator<T>;
    
} // namespace wry
#endif /* epoch_allocator_hpp */
