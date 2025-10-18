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
    
    struct alignas(8) GlobalState {
        
        uint16_t pins_current; // Pins in the current epoch
        uint16_t pins_prior;   // Pins the previous epoch
        Epoch current;         // The current epoch
        
        // The epoch can ADVANCE only if the previous epoch has zero pins.
        // On advancement, existing pins move from current to previous epoch.
        // The epoch can only advance again once all threads have unpinned or
        // repinned into the next epoch.
        
        GlobalState try_advance() const {
            if (pins_prior)
                return *this;
            return GlobalState {
                0,
                pins_current,
                current + 1,
            };
        }
        
        GlobalState pin() const {
            assert((uint16_t)(pins_current + 1)); // wrapping not allowed
            return GlobalState {
                (uint16_t)(pins_current + 1),
                pins_prior,
                current
            };
        }
        
        GlobalState unpin(Epoch occupied) const {
            if (current == occupied) {
                assert(pins_current);
                return GlobalState {
                    (uint16_t)(pins_current - 1),
                    pins_prior,
                    current
                };
            } else {
                assert(current == occupied + 1);
                assert(pins_prior);
                return GlobalState {
                    pins_current,
                    (uint16_t)(pins_prior - 1),
                    current
                };
            }
            return *this;
        }
                
    };
    
    struct Service {
    
        Atomic<GlobalState> state;
        
        Epoch pin() {
            GlobalState expected = state.load(Ordering::RELAXED);
            for (;;) {
                GlobalState desired = expected.try_advance().pin();
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::ACQUIRE,
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }
        
        Epoch unpin(Epoch occupied) {
            GlobalState expected = state.load(Ordering::RELAXED);
            for (;;) {
                GlobalState desired = expected.unpin(occupied).try_advance();
                if (state.compare_exchange_weak(expected,
                                                desired,
                                                Ordering::RELEASE,
                                                Ordering::RELAXED))
                    return desired.current;
            }
        }
        
        Epoch repin(Epoch occupied) {
            GlobalState expected = state.load(Ordering::RELAXED);
            for (;;) {
                GlobalState desired = expected.unpin(occupied).try_advance().pin();
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
