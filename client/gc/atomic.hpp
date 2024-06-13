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
#include <mach/mach_time.h>
#include <os/os_sync_wait_on_address.h>

#include <cstdlib>
#include <cstdio>

#endif  // defined(__APPLE__)

namespace gc {

    // We define our own Atomic to
    // - improve on libc++'s implementation wait on appleOS
    // - provide a customization point
    // - remove error-prone SEQ_CST defaults
    // - remove error-prone cast / assignment
    // - improve wait / wake interface

    enum class Order {
        RELAXED = __ATOMIC_RELAXED,
        ACQUIRE = __ATOMIC_ACQUIRE,
        RELEASE = __ATOMIC_RELEASE,
        ACQ_REL = __ATOMIC_ACQ_REL,
    };

    template<typename T>
    struct Atomic {
        
        static_assert(sizeof(T) <= 8);
        
        T value;
        
        constexpr Atomic() : value() {}
        explicit constexpr Atomic(T desired) : value(desired) {}
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        
        T load(Order order) const {
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
        \
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

#undef X

#if defined(__APPLE__)
        
        [[nodiscard]] T wait(T expected, Order order) {
            uint64_t buffer;
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    return discovered;
                }
                int count
                = os_sync_wait_on_address(&value, buffer, sizeof(T),
                                          OS_SYNC_WAIT_ON_ADDRESS_NONE);
                if (count < 0) switch (errno) {
                    case EINTR:
                    case EFAULT:
                        continue;
                    default:
                        perror(__PRETTY_FUNCTION__);
                        abort();
                }
            }
        }
        
        [[nodiscard]] T wait_until(T expected, Order order, uint64_t deadline) {
            uint64_t buffer;
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    return discovered;
                }
                
                int count = os_sync_wait_on_address_with_deadline
                (&value, buffer, sizeof(T),OS_SYNC_WAIT_ON_ADDRESS_NONE,
                 OS_CLOCK_MACH_ABSOLUTE_TIME, deadline);
                
                // TODO:
                // to correctly retry after false wakes we need to convert to
                // a deadline wait
                
                if (count < 0) switch (errno) {
                    case ETIMEDOUT:
                        return expected;
                    case EINTR:
                    case EFAULT:
                        continue;
                    default:
                        perror(__PRETTY_FUNCTION__);
                        abort();
                }
                
            }
                
        }
        
        void notify_one() {
            int result = os_sync_wake_by_address_any(&value, sizeof(T), OS_SYNC_WAKE_BY_ADDRESS_NONE);
            if (result != 0) switch (errno) {
                case ENOENT:
                    return;
                default:
                    perror(__PRETTY_FUNCTION__);
                    abort();
            }
        }

        void notify_all() {
            int result = os_sync_wake_by_address_all(&value, sizeof(T), OS_SYNC_WAKE_BY_ADDRESS_NONE);
            if (result != 0) switch (errno) {
                case ENOENT:
                    return;
                default:
                    perror(__PRETTY_FUNCTION__);
                    abort();
            }
        }

#endif // defined(__APPLE__)
    
    }; // template<typename> struct Atomic
    
} // namespace gc

#endif /* atomic_hpp */
