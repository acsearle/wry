//
//  epoch_allocator.hpp
//  client
//
//  Created by Antony Searle on 13/10/2025.
//

#ifndef epoch_allocator_hpp
#define epoch_allocator_hpp

#include "atomic.hpp"
#include "bump_allocator.hpp"

namespace wry {
    
    namespace epoch {
        
        // Cooperative epoch advancement
        
        // Bibliography:
        //
        // Crossbeam https://github.com/crossbeam-rs/crossbeam
                                        
        // We steal the pinning notation from Rust's Crossbeam.
        
        // We pack all the state into a single word.  This simplifies atomicity
        // and in particular memory ordering.
        
        // Crossbeam uses a list of nodes for each thread to publish its local
        // epoch.  To advance the epoch, a thread must traverse this list to ensure
        // nobody is still in the previous epoch; and sequential consistent
        // operations and fences are required to coordinate the multiple words
        // involved.  Non-thread pins must allocate their own nodes.
        //
        // We don't anticipate enough contention on the global state to make
        // that worthwhile.  If it proves to be an issue, a half-measure would be
        // to have a fixed sized list that threads spread over.
        
        using Epoch = uint32_t;

        struct Service {
            
            // wrapping not allowed
            static uint16_t _decrement_no_overflow(uint16_t x) {
                if (x == 0) [[unlikely]]
                    abort();
                return x - 1;
            }
            
            // wrapping not allowed
            static uint16_t _increment_no_overflow(uint16_t x) {
                uint16_t y = x + 1;
                if (y == 0) [[unlikely]]
                    abort();
                return y;
            }
            
            // wrapping not allowed
            // 0 -> 1 not allowed
            static uint16_t _nonzero_increment_no_overflow(uint16_t x) {
                if (x == 0) [[unlikely]]
                    abort();
                uint16_t y = x + 1;
                if (y == 0) [[unlikely]]
                    abort();
                return y;
            }
            
            struct alignas(8) State {
                
                // TODO: If we want to wait on epoch, the layout may matter to
                // Linux 32-bit futex.
                
                Epoch current;         // The current epoch
                uint16_t pins_current; // Pins in the current epoch
                uint16_t pins_prior;   // Pins in the prior epoch
                
                // Compute the prior epoch
                Epoch prior() const { return current - 1; }
                
                // Validate that a given pinned epoch is consistent with this state;
                // that is, it is either the current or prior epoch, and the
                // corresponding pin count is not zero.
                bool validate(Epoch occupied) const {
                    return (((occupied == current) && pins_current)
                            || ((occupied == prior()) && pins_prior));
                }
                
                // The epoch advances if and only if the previous epoch has zero
                // pins.  On advancement, the epoch increments, and the current
                // pins move to previous pins. The epoch can only advance again
                // once all participants have unpinned or repinned into the next
                // epoch.
                
                [[nodiscard]] State try_advance() const {
                    if (pins_prior)
                        // Prior epoch is still pinned; we can not advance
                        return *this;
                    else
                        // Prior epoch is not pinned; we can advance the epoch
                        return State {
                            .current = current + 1, // Wrapping permitted
                            .pins_current = 0,
                            .pins_prior = pins_current,
                        };
                }
                
                // New pins are applied to the current epoch
                // Aborts if the current pin count wraps; a maximum of 2**16-1 pins
                // are permitted.
                
                [[nodiscard]] State pin() const {
                    return State {
                        .current = current,
                        .pins_current = _increment_no_overflow(pins_current),
                        .pins_prior = pins_prior,
                    };
                }
                
                // We unpin a specific epoch.
                // Aborts if the specified epoch has zero pin count, or is not the
                // current or prior epoch.
                
                State unpin(Epoch occupied) const {
                    if (occupied == current) {
                        return State {
                            .current = current,
                            .pins_current = _decrement_no_overflow(pins_current),
                            .pins_prior = pins_prior,
                        };
                    } else if (occupied == prior()) {
                        return State {
                            .current = current,
                            .pins_current = pins_current,
                            .pins_prior = _decrement_no_overflow(pins_prior),
                        };
                    } else [[unlikely]] {
                        abort();
                    }
                }
                
                // Explicit pins can pin a specific epoch so long as somebody else
                // is currently pinning it, usually the calling thread.  In
                // particular, it never increases .pins_prior from zero.  This
                // property is important for making advancement safe.
                
                [[nodiscard]] State pin_explicit(Epoch occupied) const {
                    if (occupied == current) {
                        return State {
                            .current = current,
                            .pins_current = _nonzero_increment_no_overflow(pins_current),
                            .pins_prior = pins_prior,
                        };
                    } else if (occupied == prior()) {
                        return State {
                            .current = current,
                            .pins_current = pins_current,
                            .pins_prior = _nonzero_increment_no_overflow(pins_prior),
                        };
                    } else [[unlikely]] {
                        abort();
                    }
                }
                
                
            };
            
            Atomic<State> state;
            
            // Operations on the atomic state are obstruction-free.
            
            // The epoch system itself cannot progress if any pin is held
            // indefinitely, and is thus blocking.
            
            [[nodiscard]] Epoch pin() {
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    State desired = expected.try_advance().pin();
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::ACQUIRE,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return desired.current;
                    }
                }
            }
            
            void pin_explicit(Epoch occupied) {
                printf("epoch::Service::pin_explicit(%u)\n", occupied);
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    printf("expected {current %u, pins_current %u, pins_prior %u}\n", expected.current, expected.pins_current, expected.pins_prior);
                    State desired = expected.try_advance().pin_explicit(occupied);
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::ACQUIRE,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return;
                    }
                }
            }
            
            Epoch unpin(Epoch occupied) {
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    State desired = expected.unpin(occupied).try_advance();
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::RELEASE,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return desired.current;
                    }
                }
            }
            
            [[nodiscard]] Epoch repin(Epoch occupied) {
                // NOTE: Even when desired == expected, it is still important to
                // perform the write to establish memory orderings
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    State desired = expected.unpin(occupied).try_advance().pin();
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::ACQ_REL,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return desired.current;
                    }
                }
            }
            
            [[nodiscard]] Epoch repin_explicit(Epoch occupied) {
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    assert(expected.validate(occupied));
                    State desired = expected.try_advance();
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::ACQ_REL,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return desired.current;
                    }
                }
            }
            
            [[nodiscard]] Epoch repin_and_wait(Epoch occupied) {
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    assert(expected.validate(occupied));
                    State desired = expected.try_advance();
                    if (state.compare_exchange_weak(expected,
                                                    desired,
                                                    Ordering::ACQ_REL,
                                                    Ordering::RELAXED)) {
                        if (expected.current != desired.current) {
                            state.notify_all();
                        } else {
                            state.wait(desired, Ordering::ACQUIRE);
                        }
                        return desired.current;
                    }
                }
            }
            
            Epoch pin_and_unpin() {
                State expected = state.load(Ordering::RELAXED);
                for (;;) {
                    State desired = expected.try_advance();
                    if (state.compare_exchange_weak(expected, desired, Ordering::ACQ_REL, Ordering::RELAXED)) {
                        if (expected.current != desired.current)
                            state.notify_all();
                        return desired.current;
                    }
                }
            }
            
            Epoch load_acquire() {
                State expected = state.load(Ordering::ACQUIRE);
                return expected.current;
            }
            
            
            
        }; // struct Service
        
        // Usage:
        //
        // pin();
        // void* buffer = epoch::allocate(count);
        // ...
        // unpin();
        // ...
        // < buffer reclaimed >
        
        inline constinit Service allocator_global_service = {};
        
        struct LocalState {
            bump::Slab* _Nullable bump_alternate = {};
            Epoch known = {};
            bool is_pinned = {};
            
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
            
            void repin_and_wait() {
                assert(is_pinned);
                _update_with(allocator_global_service.repin_and_wait(known));
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

        inline void repin_this_thread_and_wait_for_advancement() {
            allocator_local_state.repin_and_wait();
        }
        
        // Pin the local thread's known epoch again.  The returned epoch can
        // be used to unpin that epoch on a different thread.
        //
        // This is used to tie the epoch to a non-thread scope, such as the
        // lifetime of the root of a tree of jobs.
        
        [[nodiscard]] inline auto
        pin_explicit() -> Epoch {
            printf("pin_explicit\n");
            printf("allocator_local_state.is_pinned %d", allocator_local_state.is_pinned);
            assert(allocator_local_state.is_pinned);
            printf("allocator_local_state.known %u\n", allocator_local_state.known);
            Epoch pinned = allocator_local_state.known;
            allocator_global_service.pin_explicit(pinned);
            return pinned;
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
