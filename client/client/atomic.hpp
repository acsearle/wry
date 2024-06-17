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

#include <atomic>

namespace wry {

    // We define our own Atomic to
    // - improve on libc++'s implementation wait on appleOS
    // - provide a customization point
    // - remove error-prone SEQ_CST defaults
    // - remove error-prone cast / assignment
    // - improve wait / wake interface

    // Cache line size on x86-64 and aarch64
    constexpr size_t CACHE_LINE_SIZE = 128;
    
    enum class Ordering {
        RELAXED = __ATOMIC_RELAXED,
        // CONSUME = __ATOMIC_CONSUME, // defect
        ACQUIRE = __ATOMIC_ACQUIRE,
        RELEASE = __ATOMIC_RELEASE,
        ACQ_REL = __ATOMIC_ACQ_REL,
        SEQ_CST = __ATOMIC_SEQ_CST,
    };
        
    template<typename T>
    struct Atomic {
        
        static_assert(sizeof(T) <= 8);
        
        T value;
        
        constexpr Atomic() : value{} {}
        explicit constexpr Atomic(T desired) : value(desired) {}
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        
        T load(Ordering order) const {
            T discovered;
            __atomic_load(&value, &discovered, (int)order);
            return discovered;
        }

        void store(T desired, Ordering order) {
            __atomic_store(&value, &desired, (int)order);
        }

        T exchange(T desired, Ordering order) {
            T discovered;
            __atomic_exchange(&value, &desired, &discovered, (int)order);
            return discovered;
        }
        
        bool compare_exchange_weak(T& expected,
                                   T desired,
                                   Ordering success,
                                   Ordering failure) {
            return __atomic_compare_exchange(&value,
                                             &expected,
                                             &desired,
                                             true,
                                             (int)success,
                                             (int)failure);
        }
        
        bool compare_exchange_strong(T& expected,
                                     T desired,
                                     Ordering success,
                                     Ordering failure) {
            return __atomic_compare_exchange(&value,
                                             &expected,
                                             &desired,
                                             false,
                                             (int)success,
                                             (int)failure);
        }
        
#define X(Y) \
        \
        T fetch_##Y (T operand, Ordering order) {\
            return __atomic_fetch_##Y (&value, operand, (int)order);\
        }\
        \
        T Y##_fetch(T operand, Ordering order) {\
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
        
        T wait(T expected, Ordering order) {
            static_assert(sizeof(T) <= 8);
            uint64_t buffer;
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    return discovered;
                }
                int count = os_sync_wait_on_address(&value,
                                                    buffer,
                                                    sizeof(T),
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
        
        T wait_until(T expected, Ordering order, uint64_t deadline) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer;
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    return discovered;
                }
                
                int count = os_sync_wait_on_address_with_deadline(&value,
                                                                  buffer,
                                                                  sizeof(T),
                                                                  OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                                  OS_CLOCK_MACH_ABSOLUTE_TIME,
                                                                  deadline);
                if (count < 0) switch (errno) {
                    case ETIMEDOUT:
                        return expected;
                    case EINTR:
                    case EFAULT:
                        break;
                    default:
                        perror(__PRETTY_FUNCTION__);
                        abort();
                }
                
            }
        }
        
        T wait_for(T expected, Ordering order, uint64_t timeout_ns) {
            return wait_until(expected, order, mach_absolute_time() + timeout_ns);
        }
        
        void notify_one() {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            int result = os_sync_wake_by_address_any(&value, 
                                                     sizeof(T),
                                                     OS_SYNC_WAKE_BY_ADDRESS_NONE);
            if (result != 0) switch (errno) {
                case ENOENT:
                    return;
                default:
                    perror(__PRETTY_FUNCTION__);
                    abort();
            }
        }

        void notify_all() {
            int result = os_sync_wake_by_address_all(&value, 
                                                     sizeof(T),
                                                     OS_SYNC_WAKE_BY_ADDRESS_NONE);
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
    
} // namespace wry

#endif /* atomic_hpp */
