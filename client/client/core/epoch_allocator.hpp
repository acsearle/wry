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
              
        
        
        // Cyclic group of order 2^N
        //
        // Promoting arithmetic makes uint16_t awkward to use as a cyclic group
        // This type performs some coercion to greatly improve the ergonomics
        
        struct cyc16_t {
            
            uint16_t raw;
            
            cyc16_t operator++(int) { return {raw++}; }
            cyc16_t operator--(int) { return {raw--}; }
            cyc16_t& operator++() { ++raw; return *this; }
            cyc16_t& operator--() { --raw; return *this; }
            cyc16_t operator+(int delta) const { return {(uint16_t)(raw + delta)}; }
            cyc16_t operator-(int delta) const { return {(uint16_t)(raw - delta)}; }
            int16_t operator-(cyc16_t other) const { return (int16_t)(raw - other.raw); }
            
            std::strong_ordering operator<=>(cyc16_t const& other) const {
                int difference = *this - other;
                // catch comparisons spanning more than half a cycle
                assert(std::abs(difference) < 0x4000);
                return difference <=> 0;
            }
            bool operator==(cyc16_t const& other) const = default;
            
            cyc16_t& operator+=(int delta) { raw += delta; return *this; }
            cyc16_t& operator-=(int delta) { raw -= delta; return *this; }
            
        };
        
        
        [[nodiscard]] constexpr uint16_t successor(uint16_t n) {
            if (n == UINT16_MAX) [[unlikely]] abort();
            return (uint16_t)(n + 1);
        };
        
        [[nodiscard]] constexpr uint16_t predecessor(uint16_t n) {
            if (n == 0) [[unlikely]] abort();
            return (uint16_t)(n - 1);
        }
        
        
        using Epoch = cyc16_t;
        using Count = uint16_t;
                
        // Service will be instantiated as a global singleton and its state
        // is thus atomic
        
        struct Service {
            
            struct alignas(8) State {
                
                // TODO: If we want to wait on epoch, the layout may matter to
                // Linux 32-bit futex.
                
                Epoch current;      // The current epoch
                Count pins_current; // Pins in the current epoch
                Count pins_prior;   // Pins in the prior epoch
                uint16_t waiting;   // A thread is waiting
                                
                // Validate that a given pinned epoch is consistent with this state;
                // that is, it is either the current or prior epoch, and the
                // corresponding pin count is not zero.
                bool validate(Epoch occupied) const {
                    return (((occupied == current) && pins_current)
                            || ((occupied+1 == current) && pins_prior));
                }
                
                // The epoch advances if and only if the previous epoch has zero
                // pins.  On advancement, the epoch increments, and the current
                // pins move to previous pins. The epoch can only advance again
                // once all participants have unpinned or repinned into the next
                // epoch.
                
                [[nodiscard]] State try_advance() const {
                    return State{
                        .current = pins_prior ? current : current+1,
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
                
                [[nodiscard]] State unpin(Epoch occupied) const {
                    if ((occupied != current) && (occupied+1 != current)) [[unlikely]] {
                        abort();
                    }
                    return State {
                        .current = current,
                        .pins_current = (occupied == current) ? predecessor(pins_current) : pins_current,
                        .pins_prior = (occupied+1 == current) ? predecessor(pins_prior) : pins_prior,
                        .waiting = waiting
                    };
                }
                
                // Set the "waiting"
                [[nodiscard]] State wait(Epoch expected) const {
                    return State {
                        .current = current,
                        .pins_current = pins_current,
                        .pins_prior = pins_prior,
                        .waiting = (current == expected),
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
            
            // Wait does not return until "expected" is not the current epoch.
            // It does not establish a happens-before relationship; that is
            // pin's job.  A pinned thread can safely wait on the epoch it
            // pinned, but it will deadlock on the next epoch.  (Debug mode will
            // detect the deadlock.)
            void wait(Epoch expected);
            
            // TODO: consistency with std::atomic::wait and wry::Atomic::wait
            // TODO: current status of libc++'s wait implementation (used to be no damn good)
            
            
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
        
        inline void Service::wait(Epoch expected) {
            
            // Detect if this_thread is pinning expected-1,
            // and thus the epoch can never advance
            assert(!allocator_local_state.is_pinned
                   || (expected != allocator_local_state.known + 1));
            
            // The atomic operations can fail due to the pin counts changing,
            // or entirely spuriously, but all eventualities can be handled by
            // a simple retry loop.
            
            State observed = state.load(Ordering::RELAXED);
            while (observed.current == expected) {
                if (!observed.waiting) {
                    State desired = observed.wait(expected);
                    if (state.compare_exchange_weak(observed,
                                                    desired,
                                                    Ordering::RELAXED,
                                                    Ordering::RELAXED)) {
                        observed = desired;
                    }
                } else {
                    state.wait(observed, Ordering::RELAXED);
                    observed = state.load(Ordering::RELAXED);
                }
            }
        }
        
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
