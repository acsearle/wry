//
//  epoch_service.hpp
//  client
//
//  Created by Antony Searle on 11/10/2025.
//

#ifndef epoch_service_hpp
#define epoch_service_hpp

#include "assert.hpp"
#include "atomic.hpp"
#include "utility.hpp"

namespace wry::epoch {
    
    // Cooperative epoch advancement
    
    struct Epoch {
        
        uint32_t data;
        
        bool operator==(Epoch const&) const = default;
        
        Epoch operator+(int i) const { return Epoch{data + i}; }
        
    };
    
    // We pack all the state into a single word.  This simplifies atomicity
    // and in particular memory ordering.

    // Rust.crossbeam uses a list of nodes for each thread to publish its local
    // epoch.  To advance the epoch, a thread must traverse this list to ensure
    // nobody is still in the previous epoch; and sequential consistent
    // operations and fences are required to coordinate the multiple words
    // involved.
    // We don't anticipate enough contention on the GlobalState to make
    // that worthwhile.
    
    // We steal the pinning notation from crossbeam.
    
    struct Service {
        
        struct alignas(8) State {
            
            uint16_t pins_current; // Pins in the current epoch
            uint16_t pins_prior;   // Pins the previous epoch
            Epoch current;         // The current epoch
            
            // The epoch can ADVANCE only if the previous epoch has zero pins.
            // On advancement, existing pins move from current to previous epoch.
            // The epoch can only advance again once all threads have unpinned or
            // repinned into the next epoch.
            
            [[nodiscard]] State try_advance() const {
                if (pins_prior)
                    return *this;
                return State {
                    .pins_current = 0,
                    .pins_prior = pins_current,
                    .current = current + 1,
                };
            }
            
            [[nodiscard]] State pin() const {
                assert((uint16_t)(pins_current + 1)); // wrapping not allowed
                return State {
                    .pins_current = (uint16_t)(pins_current + 1),
                    .pins_prior = pins_prior,
                    .current = current
                };
            }
            
            [[nodiscard]] State pin_explicit(Epoch known) const {
                if (known == current) {
                    assert(pins_current);
                    assert((uint16_t)(pins_current + 1)); // wrapping not allowed
                    return State {
                        .pins_current = (uint16_t)(pins_current + 1),
                        .pins_prior = pins_prior,
                        .current = current
                    };
                } else if (known + 1 == current) {
                    assert(pins_prior); // must be occupied to increment
                    assert((uint16_t)(pins_prior + 1)); // wrapping not allowed
                    return State {
                        .pins_current = pins_current,
                        .pins_prior = (uint16_t)(pins_prior + 1),
                        .current = current
                    };
                } else {
                    abort();
                }
            }
            
            State unpin(Epoch occupied) const {
                if (occupied == current) {
                    assert(pins_current);
                    return State {
                        (uint16_t)(pins_current - 1),
                        pins_prior,
                        current
                    };
                } else if (occupied + 1 == current) {
                    assert(pins_prior);
                    return State {
                        pins_current,
                        (uint16_t)(pins_prior - 1),
                        current
                    };
                } else {
                    abort();
                }
            }
            
            bool validate(Epoch occupied) const {
                return ((occupied == current) && pins_current)
                       || ((occupied + 1 == current) && pins_prior);
            }
            
        };
    
        Atomic<State> state;
        
        [[nodiscard]] Epoch pin() {
            State expected = state.load(Ordering::RELAXED);
            for (;;) {
                State desired = expected.try_advance().pin();
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::ACQUIRE,
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }
        
        [[nodiscard]] Epoch pin_explicit(Epoch occupied) {
            State expected = state.load(Ordering::RELAXED);
            for (;;) {
                State desired = expected.try_advance().pin_explicit(occupied);
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::ACQUIRE,
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }
        
        Epoch unpin(Epoch occupied) {
            State expected = state.load(Ordering::RELAXED);
            for (;;) {
                State desired = expected.unpin(occupied).try_advance();
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::RELEASE,
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }
        
        [[nodiscard]] Epoch repin(Epoch occupied) {
            State expected = state.load(Ordering::RELAXED);
            for (;;) {
                State desired = expected.unpin(occupied).try_advance().pin();
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::ACQ_REL,
                                                Ordering::RELAXED))
                    return desired.current;
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
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }

        
        
    }; // struct Service
    
} // namespace wry::epoch


#endif /* epoch_service_hpp */
