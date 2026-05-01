//
//  epoch_allocator.hpp
//  client
//
//  Created by Antony Searle on 13/10/2025.
//

#ifndef epoch_allocator_hpp
#define epoch_allocator_hpp

#include "epoch.hpp"
#include "bump_allocator.hpp"

namespace wry {

    namespace epoch {

        // Usage:
        //
        // pin_this_thread();
        // void* buffer = epoch::allocate(count);
        // ...
        // unpin_this_thread();
        // ...
        // < buffer eventually reused >

        // LocalState will be instantiated as a thread_local variable and not
        // exposed to other threads
        
        // TODO: Disentangle the thread_local part of the Service from
        // the Allocator.  Possibly, push _update_with down to the free
        // functions.  But, as things named "pin" with different decorator actions
        // proliferate, we are creating footguns.
        
        struct LocalState {
                    
            Epoch known = {};
            bool is_pinned = {};

            // cyclic queue of Epochs to reuse once we have advanced epochs
            // enough
            enum : std::size_t { SIZE = 3 };
            bump::Slab* _Nullable alternates[SIZE] = {};

            void _update_with(Epoch observed) {
                if (observed != known) {

                    // TODO: It would be nice for BumpAllocator to just use the
                    // head of the list, but it's also nice for BumpAllocator to
                    // be a separate entity.

                    // TODO: We can explicitly tag these BumpAllocators with their
                    // associated epoch to decide when to re-use them

                    // swap head
                    alternates[0] = bump::this_thread_state.exchange_head_and_restart(alternates[0]);
                    // rotate buffer
                    std::rotate(alternates, alternates + 1, alternates + SIZE);
                    known = observed;
                }
            }

            void pin() {
                assert(!is_pinned);
                _update_with(global_service.pin());
                is_pinned = true;
            }
            
            // TODO: Poison the thread_local allocator state to catch unpinned
            // use

            void unpin() {
                assert(is_pinned);
                _update_with(global_service.unpin(known));
                is_pinned = false;
            }

            void repin() {
                assert(is_pinned);
                _update_with(global_service.repin(known));
            }

        };

        constinit inline thread_local LocalState local_state = {};

        // Wrapper around Service::wait that adds a debug assertion catching
        // the case where this thread's own pin would prevent the awaited
        // advancement.
        inline void wait(Epoch expected) {

            // Detect if this_thread is pinning expected-1,
            // and thus the epoch can never advance
            assert(!local_state.is_pinned
                   || (expected != local_state.known + 1));

            global_service.wait(expected);
        }

        // Keep the epoch pinned while a thread is awake

        inline void pin_this_thread() {
            local_state.pin();
        }

        inline void unpin_this_thread() {
            local_state.unpin();
        }

        inline void repin_this_thread() {
            local_state.repin();
        }

        [[nodiscard]] inline void* _Nonnull allocate(size_t count) {
            assert(local_state.is_pinned);
            return bump::allocate(count);
        }

        inline void deallocate(void* _Nullable) {
            assert(local_state.is_pinned);
            // no op
        }

    } // namespace wry::epoch

    using EpochAllocated = BumpAllocated;
    template<typename T> using EpochAllocator = BumpAllocator<T>;

} // namespace wry
#endif /* epoch_allocator_hpp */
