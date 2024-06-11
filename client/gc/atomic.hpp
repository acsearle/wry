//
//  atomic.hpp
//  client
//
//  Created by Antony Searle on 11/6/2024.
//

#ifndef atomic_hpp
#define atomic_hpp


#if defined(__APPLE__)

#include <errno.h>
#include <os/os_sync_wait_on_address.h>

#include <cstdlib>
#include <cstdio>

#endif  // defined(__APPLE__)

namespace gc {
    
    // Mostly-compatible with std::atomic
    //
    // - all clang builtins
    // - shorter order
    

    enum class Order {
        RELAXED = __ATOMIC_RELAXED,
        ACQUIRE = __ATOMIC_ACQUIRE,
        RELEASE = __ATOMIC_RELEASE,
        ACQ_REL = __ATOMIC_ACQ_REL,
    };

    template<typename T>
    struct Atomic {
        
        T value;
        
        T load(Order order) {
            T discovered;
            __atomic_load(&value, &discovered, (int)order);
            return discovered;
        }

        void store(T desired, Order order) {
            __atomic_store(&value, &desired, (int)order);
        }

        T exchange(T desired, Order order) {
            T discovered;
            __atomic_exchange(&value, &desired, &discovered, (int)order);
            return discovered;
        }
        
        bool compare_exchange_weak(T& expected,
                                   T desired,
                                   Order success,
                                   Order failure) {
            return __atomic_compare_exchange(&value,
                                             &expected,
                                             &desired,
                                             true,
                                             (int)success,
                                             (int)failure);
        }
        
        bool compare_exchange_strong(T& expected,
                                     T desired,
                                     Order success,
                                     Order failure) {
            return __atomic_compare_exchange(&value,
                                             &expected,
                                             &desired,
                                             false,
                                             (int)success,
                                             (int)failure);
        }
        
#define X(Y) \
T fetch_##Y (T operand, Order order) {\
return __atomic_fetch_##Y (&value, operand, (int)order);\
}\
\
T Y##_fetch(T operand, Order order) {\
return __atomic_##Y##_fetch (&value, operand, (int)order);\
}
        
        X(add)
        X(and)
        X(max)
        X(min)
        X(nand)
        X(or)
        X(sub)
        X(xor)
        
#if defined(__APPLE__)
        
        [[nodiscard]] T wait(T expected, Order order) {
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&expected, &discovered, sizeof(T)))
                    return discovered;
                int result = os_sync_wait_on_address(&value, expected, sizeof(T), OS_SYNC_WAIT_ON_ADDRESS_NONE);
                if ((result < 0) && !((errno == EINTR) || (errno == EFAULT))) {
                    perror(__PRETTY_FUNCTION__);
                    abort();
                }
            }
        }
        
        void notify_one() {
            int result = os_sync_wake_by_address_any(&value, sizeof(T), OS_SYNC_WAKE_BY_ADDRESS_NONE);
            if ((result != 0) && (errno != ENOENT)) {
                perror(__PRETTY_FUNCTION__);
                abort();
            }
                
        }
        
#endif // defined(__APPLE__)
    
    }; // template<typename> struct Atomic
    
} // namespace gc

#endif /* atomic_hpp */
