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
        
        // The ordering induced by the Epoch is as follows:
        
        // The epoch is nondecreasing and atomic.  Specifically, all threads
        // agree on a single, total order of writes to the epoch.
        
        // Thread A pins epoch E.
        // E was the current epoch at that moment.
        
        // Thread A writes.
        
        // Thread A unpins, or repins, epoch E'
        // This is a release barrier.
        // E' was the current epoch at that moment.
        // E' is either E or E+1.
        
        // Thread B pins epoch F.
        // F was the current epoch at that moment.
        // This is an acquire barrier.
        
        // By modificaton order consistency + nondecreasing:
        // If F > E + 1 >= E', then B wrote F after A wrote E'
        
        // Thus epoch E in thread A happens before epoch F in thread B if
        // F > E + 1
        
        struct Epoch {
            uint16_t raw;
            bool operator==(Epoch const&) const = default;            
        };
        
        [[nodiscard]] constexpr Epoch successor(Epoch epoch) {
            // Wrapping is permitted
            return Epoch{
                .raw = (uint16_t)(epoch.raw + 1),
            };
        }
        
        [[nodiscard]] constexpr Epoch operator+(Epoch epoch, int n) {
            return Epoch{
                .raw = (uint16_t)(epoch.raw + n)
            };
        }
        
        struct Count {
            uint16_t raw;
            explicit operator bool() const { return (bool)raw; }
        };
        
        [[nodiscard]] constexpr Count successor(Count count) {
            // Wrapping is not permitted
            if (count.raw == UINT16_MAX) [[unlikely]] {
                abort();
            }
            return Count{
                .raw = (uint16_t)(count.raw + 1),
            };
        };
        
        [[nodiscard]] constexpr Count predecessor(Count count) {
            // Wrapping is not permitted
            if (count.raw == 0) [[unlikely]] {
                abort();
            }
            return Count{
                .raw = (uint16_t)(count.raw - 1),
            };
        }
        
        [[nodiscard]] constexpr Count successor_of_nonzero(Count count) {
            if ((count.raw == 0) || (count.raw == UINT16_MAX)) [[unlikely]] {
                abort();
            }
            return Count{
                .raw = (uint16_t)(count.raw + 1),
            };
        }
        
        // Service will be instantiated as a global singleton and its state
        // is thus atomic
        
        struct Service {
            
            struct alignas(8) State {
                
                // TODO: If we want to wait on epoch, the layout may matter to
                // Linux 32-bit futex.
                
                Epoch current;      // The current epoch
                Count pins_current; // Pins in the current epoch
                Count pins_prior;   // Pins in the prior epoch
                uint16_t waiting;   // Boolean somebody is waiting
                
                // Validate that a given pinned epoch is consistent with this state;
                // that is, it is either the current or prior epoch, and the
                // corresponding pin count is not zero.
                bool validate(Epoch occupied) const {
                    return (((occupied == current) && pins_current)
                            || ((successor(occupied) == current) && pins_prior));
                }
                
                // The epoch advances if and only if the previous epoch has zero
                // pins.  On advancement, the epoch increments, and the current
                // pins move to previous pins. The epoch can only advance again
                // once all participants have unpinned or repinned into the next
                // epoch.
                
                [[nodiscard]] State try_advance() const {
                    return State{
                        .current = pins_prior ? current : successor(current),
                        .pins_current = pins_prior ? pins_current : Count{0},
                        .pins_prior = pins_prior ? pins_prior : pins_current,
                        .waiting = pins_prior ? waiting : uint16_t{0},
                    };
                }
                
                // New pins are applied to the current epoch
                // Aborts if the current pin count wraps; a maximum of 2**16-1 pins
                // are permitted.
                
                [[nodiscard]] State pin() const {
                    return State {
                        .current = current,
                        .pins_current = successor(pins_current),
                        .pins_prior = pins_prior,
                        .waiting = waiting,
                    };
                }
                
                // We unpin a specific epoch.
                // Aborts if the specified epoch has zero pin count, or is not the
                // current or prior epoch.
                
                State unpin(Epoch occupied) const {
                    if (occupied != current && successor(occupied) != current) [[unlikely]] {
                        abort();
                    }
                    return State {
                        .current = current,
                        .pins_current = (occupied == current) ? predecessor(pins_current) : pins_current,
                        .pins_prior = (successor(occupied) == current) ? predecessor(pins_prior) : pins_prior,
                        .waiting = waiting
                    };
                }
                
                State wait() const {
                    return State {
                        .current = current,
                        .pins_current = pins_current,
                        .pins_prior = pins_prior,
                        .waiting = uint16_t{1},
                    };
                }

            };
            
            Atomic<State> state;
            
            // Operations on the atomic state are obstruction-free.
            
            // The epoch system itself cannot progress if any pin is held
            // indefinitely, and is thus blocking.
                        
            [[nodiscard]] Epoch pin() {
                State expected = state.load(Ordering::RELAXED);
                State desired;
                do {
                    desired = expected.pin();
                } while (!state.compare_exchange_weak(expected,
                                                      desired,
                                                      Ordering::ACQUIRE,
                                                      Ordering::RELAXED));
                if (expected.waiting && !desired.waiting) {
                    state.notify_all();
                }
                return desired.current;
            }
            
            Epoch unpin(Epoch occupied) {
                State expected = state.load(Ordering::RELAXED);
                State desired;
                do {
                    desired = expected.unpin(occupied).try_advance();
                } while (!state.compare_exchange_weak(expected,
                                                      desired,
                                                      Ordering::RELEASE,
                                                      Ordering::RELAXED));
                if (expected.waiting && !desired.waiting) {
                    state.notify_all();
                }
                return desired.current;
            }
            
            [[nodiscard]] Epoch repin(Epoch occupied) {
                // SAFETY: When desired == expected, we still perform the write
                // to establish memory orderings
                State expected = state.load(Ordering::RELAXED);
                State desired;
                do {
                    desired = expected.unpin(occupied).try_advance().pin();
                } while (!state.compare_exchange_weak(expected,
                                                      desired,
                                                      Ordering::ACQ_REL,
                                                      Ordering::RELAXED));
                if (expected.waiting && !desired.waiting) {
                    state.notify_all();
                }
                return desired.current;
            }
            
            Epoch load_acquire() {
                State expected = state.load(Ordering::ACQUIRE);
                return expected.current;
            }
            
            
            
        }; // struct Service
        
        inline constinit Service allocator_global_service = {};
        
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
        
        constinit inline thread_local LocalState allocator_local_state = {};
        
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

        [[nodiscard]] inline void* _Nonnull allocate(size_t count) {
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
