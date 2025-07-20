//
//  atomic.hpp
//  client
//
//  Created by Antony Searle on 11/6/2024.
//

#ifndef atomic_hpp
#define atomic_hpp

#include <cstdlib>
#include <cstdio>

#if defined(__APPLE__)
#include <errno.h>
#include <mach/mach_time.h>
// os_sync_wait_on_address
// os_sync_wait_on_address_dealine
// os_sync_wake_by_address_any
// os_sync_wake_by_address_all
#include <os/os_sync_wait_on_address.h>
#endif  // defined(__APPLE__)

#if defined(WIN32)
#include <synchapi.h>
// WaitOnAddress
// WakeByAddressSingle
// WakeByAddressAll
#endif

#if defined(LINUX)
#include <linux/futex.h>      /* Definition of FUTEX_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>
#endif

#include <atomic>

namespace wry {

    // We define our own Atomic to
    // - provide a legal customization point
    // - remove error-prone casts, assignments and defaults (of sequential
    //   consist memory ordering)
    // - improve the interface and libc++'s implementation of wait/notify

    // The implementation depends heavily on the GCC intrinsics
    // TODO: extend to MSVC _Interlocked[op][width]_[ordering]

    // TODO: architecture specific cache line size
    constexpr size_t CACHE_LINE_SIZE = 128;
    
    enum class Ordering {
        RELAXED = __ATOMIC_RELAXED,
        CONSUME = __ATOMIC_CONSUME,
        ACQUIRE = __ATOMIC_ACQUIRE,
        RELEASE = __ATOMIC_RELEASE,
        ACQ_REL = __ATOMIC_ACQ_REL,
        SEQ_CST = __ATOMIC_SEQ_CST,
    };
    
    // Compare std::cv_status
    enum class AtomicWaitResult {
        NO_TIMEOUT,
        TIMEOUT,
    };
    
    template<typename T>
    struct Atomic {
        
        static_assert(__atomic_always_lock_free(sizeof(T), nullptr));
        
        T value;
        
        constexpr Atomic() : value{} {}
        explicit constexpr Atomic(T desired) : value{desired} {}
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
        
        void wait(T& expected, Ordering order) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer = {};
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = this->load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    expected = discovered;
                    return;
                }
                int count = os_sync_wait_on_address(&value,
                                                    buffer,
                                                    sizeof(T),
                                                    OS_SYNC_WAIT_ON_ADDRESS_NONE);
                if (count < 0) switch (errno) {
                    case EINTR:
                    case EFAULT:
                        break;
                    default:
                        perror(__PRETTY_FUNCTION__);
                        abort();
                }
            }
        }
        
        AtomicWaitResult wait_until(T& expected, Ordering order, uint64_t deadline) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer = {};
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = this->load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    expected = discovered;
                    return AtomicWaitResult::NO_TIMEOUT;
                }                
                int count = os_sync_wait_on_address_with_deadline(&(this->value),
                                                                  buffer,
                                                                  sizeof(T),
                                                                  OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                                  OS_CLOCK_MACH_ABSOLUTE_TIME,
                                                                  deadline);
                if (count < 0) switch (errno) {
                    case ETIMEDOUT:
                        return AtomicWaitResult::TIMEOUT;
                    case EINTR:
                    case EFAULT:
                        break;
                    default:
                        perror(__PRETTY_FUNCTION__);
                        abort();
                }
            }
        }
        
        AtomicWaitResult wait_for(T& expected, Ordering order, uint64_t timeout_ns) {
            struct mach_timebase_info info;
            mach_timebase_info(&info);
            return wait_until(expected, order, mach_absolute_time() + timeout_ns / info.numer * info.denom);
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

#if defined(WIN32)
        
        void wait(T& expected, Ordering order) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    expected = discovered;
                    return;
                }
                BOOL result = WaitOnAddress(&value,
                                            &expected,
                                            sizeof(T),
                                            INFINITE
                                            );
                if (!result) {
                    DWORD lastError = GetLastError();
                    switch (lastError) {
                        default:
                            fprintf(stderr, "%s: %lx\n", __PRETTY_FUNCTION__, lastError);
                            abort();
                    }
                }
            }
        }
                
        AtomicWaitResult wait_for(T& expected, Ordering order, uint64_t timeout_ns) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer;
            for (;;) {
                T discovered = load(order);
                if (__builtin_memcmp(&buffer, &discovered, sizeof(T))) {
                    expected = discovered;
                    return AtomicWaitResult::NO_TIMEOUT;
                }
                BOOL result = WaitOnAddress(&value,
                                            &expected,
                                            sizeof(T),
                                            timeout_ns / 10000000
                                            );
                if (!result) {
                    DWORD lastError = GetLastError();
                    switch (lastError) {
                        case ERROR_TIMEOUT:
                            return AtomicWaitResult::TIMEOUT;
                        default:
                            fprintf(stderr, "%s: %lx\n", __PRETTY_FUNCTION__, lastError);
                            abort();
                    }
                }
            }
        }
        
        void notify_one() {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            WakeByAddressSingle(&value);
        }
        
        void notify_all() {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            WakeByAddressAll(&value);
        }
        
#endif // defined(WIN32)
        
#if defined(__linux__)
        
        void wait(T& expected, Ordering order) {
            static_assert(sizeof(T) == 4);
            syscall(SYS_FUTEX, &value, FUTEX_WAIT_PRIVATE, &expected, nullptr, nullptr, 0);
            
        }
        
        void notify_one() {
            static_assert(sizeof(T) == 4);
            syscall(SYS_FUTEX, &value, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
        }

        void notify_all() {
            static_assert(sizeof(T) == 4);
            syscall(SYS_FUTEX, &value, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
        }

#endif // defined(__linux__)
        
    }; // template<typename> struct Atomic
    
} // namespace wry

#endif /* atomic_hpp */
