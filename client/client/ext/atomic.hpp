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
#include <os/os_sync_wait_on_address.h>
#endif  // defined(__APPLE__)

#if defined(WIN32)
#include <synchapi.h>
#endif // defined(WIN32)

#if defined(LINUX)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif // defined(LINUX)

#include <atomic>

#include "stdint.hpp"

namespace wry {
    
    // We define our own Atomic to
    // - provide a legal customization point
    // - remove error-prone casts, assignments and defaults (of sequential
    //   consist memory ordering)
    // - improve the interface and implementation(*) of wait and notify
    // - provide add_fetch and other primitives supported by builtins but not
    //   std::atomic
    //
    // (*) libc++ is slow to adopt platform-specific futexes
    
    // The implementation depends heavily on GCC-style intrinsics
    // TODO: extend to MSVC _Interlocked[op][width]_[ordering]
    
    // TODO: Implementations do not support runtime choice of ordering; should
    // they therefore be template areguments?  Are we relying on inlining?
    // Should we never put order in the signature of a function whose
    // declaration has different visibility to its definition?
    
    // TODO: Userspace task switch on wait
    // Waiting, i.e. deferring to the OS scheduler, is rarely what we actually
    // want to do.  Ensure that we can do userspace task switching instead with
    // a coroutine, callback, or other.  Raw atomics are unlikely to be the
    // right level of abstraction for this.
    
    // TODO: architecture specific cache line size
    // std::hardware_destructive_interference_size seems unimplemented
    constexpr size_t CACHE_LINE_SIZE = 128;
    
    enum class Ordering {
        RELAXED = __ATOMIC_RELAXED,
        CONSUME = __ATOMIC_CONSUME, // TODO: deprecation/implementation status
        ACQUIRE = __ATOMIC_ACQUIRE,
        RELEASE = __ATOMIC_RELEASE,
        ACQ_REL = __ATOMIC_ACQ_REL,
        SEQ_CST = __ATOMIC_SEQ_CST,
    };
    
#define _WRY_ATOMIC_relaxed __ATOMIC_RELAXED
// #define _WRY_ATOMIC_consume __ATOMIC_CONSUME
#define _WRY_ATOMIC_acquire __ATOMIC_ACQUIRE
#define _WRY_ATOMIC_release __ATOMIC_RELEASE
#define _WRY_ATOMIC_acq_rel __ATOMIC_ACQ_REL
#define _WRY_ATOMIC_seq_cst __ATOMIC_SEQ_CST

    
    // Compare std::cv_status
    enum class AtomicWaitResult {
        NO_TIMEOUT,
        TIMEOUT,
    };
    
    template<typename T>
    concept AlwaysLockFreeAtomic = __atomic_always_lock_free(sizeof(T), nullptr);
    
    template<AlwaysLockFreeAtomic T>
    struct Atomic {
        
        using value_type = T;
        static constexpr bool is_always_lock_free = true;
        
        static constexpr bool _is_native = std::is_integral_v<T> || std::is_pointer_v<T>;
        using U = std::conditional_t<_is_native, T, integer_of_byte_width_t<sizeof(T)>>;

        U value;
        
        constexpr Atomic() noexcept
        : value{} {
        }
        
        explicit constexpr Atomic(T desired) noexcept
        : value{std::bit_cast<U>(desired)} {
        }
        
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        
#define MAKE_WRY_ATOMIC_LOAD(order)\
        T load_##order() const noexcept {\
            return std::bit_cast<T>(__atomic_load_n(&value, _WRY_ATOMIC_##order));\
        }

        MAKE_WRY_ATOMIC_LOAD(relaxed)
        MAKE_WRY_ATOMIC_LOAD(acquire)

        void store(T desired, Ordering order) noexcept {
            __atomic_store_n(&value, std::bit_cast<U>(desired), (int)order);
        }
        
        T exchange(T desired, Ordering order) noexcept {
            return std::bit_cast<T>(__atomic_exchange_n(&value, std::bit_cast<U>(desired), (int)order));
        }
        
        bool compare_exchange_weak(T& expected,
                                   T desired,
                                   Ordering success,
                                   Ordering failure) noexcept {
            U expected2{std::bit_cast<U>(expected)};
            bool result{__atomic_compare_exchange_n(&value,
                                                    &expected2,
                                                    std::bit_cast<U>(desired),
                                                    true,
                                                    (int)success,
                                                    (int)failure)};
            expected = std::bit_cast<T>(expected2);
            return result;
        }
        
        bool compare_exchange_strong(T& expected,
                                     T desired,
                                     Ordering success,
                                     Ordering failure) noexcept {
            U expected2{std::bit_cast<U>(expected)};
            bool result{__atomic_compare_exchange_n(&value,
                                                    &expected2,
                                                    std::bit_cast<U>(desired),
                                                    false,
                                                    (int)success,
                                                    (int)failure)};
            expected = std::bit_cast<T>(expected2);
            return result;
        }
        
#define X(Y) \
\
T fetch_##Y (T operand, Ordering order) noexcept {\
return __atomic_fetch_##Y (&value, operand, (int)order);\
}\
\
T Y##_fetch(T operand, Ordering order) noexcept {\
return __atomic_##Y##_fetch (&value, operand, (int)order);\
}
        
        // GCC builtins provide significantly more operations than std::atomic
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
        
        void wait(T& expected, Ordering order) noexcept {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer = {};
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = this->load_relaxed();
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
        
        AtomicWaitResult wait_until(T& expected, Ordering order, uint64_t deadline) noexcept {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            uint64_t buffer = {};
            __builtin_memcpy(&buffer, &expected, sizeof(T));
            for (;;) {
                T discovered = this->load_relaxed();
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
        
        AtomicWaitResult wait_for(T& expected, Ordering order, uint64_t timeout_ns) noexcept {
            struct mach_timebase_info info;
            mach_timebase_info(&info);
            return wait_until(expected, order, mach_absolute_time() + (timeout_ns * info.denom) / info.numer);
        }
        
        void notify_one() noexcept {
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
        
        void notify_all() noexcept {
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
        
        // TODO: This code sketch is untested
        
        void wait(T& expected, Ordering order) noexcept {
            uint64_t buffer = {};
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
        
        AtomicWaitResult wait_for(T& expected, Ordering order, uint64_t timeout_ns) noexcept {
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
                                            timeout_ns / 1000000
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
        
        void notify_one() noexcept {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            WakeByAddressSingle(&value);
        }
        
        void notify_all() noexcept {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            WakeByAddressAll(&value);
        }
        
#endif // defined(WIN32)
        
#if defined(__linux__)
        
        // TODO: This code sketch is untested
        
        void wait(T& expected, Ordering order) requires(sizeof(T) == 4) noexcept {
            (void) syscall(SYS_futex, &value, FUTEX_WAIT_PRIVATE, &expected, nullptr, nullptr, 0);
        }
        
        void notify_one() requires { sizeof(T) == 4 } noexcept {
            static_assert(sizeof(T) == 4);
            (void) syscall(SYS_futex, &value, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
        }
        
        void notify_all() requires { sizeof(T) == 4 } noexcept {
            static_assert(sizeof(T) == 4);
            (void) syscall(SYS_futex, &value, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
        }
        
#endif // defined(__linux__)
        
    }; // template<typename> struct Atomic
    
    template<typename T>
    void garbage_collected_scan(Atomic<T> const& x) {
        garbage_collected_scan(x.load(Ordering::ACQUIRE));
    }
    
} // namespace wry

#endif /* atomic_hpp */
