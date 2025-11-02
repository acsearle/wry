//
//  mutex.hpp
//  client
//
//  Created by Antony Searle on 7/7/2025.
//

#ifndef mutex_hpp
#define mutex_hpp

#include <os/lock.h>
#include <os/os_sync_wait_on_address.h>

#include <atomic>
#include <mutex>


namespace wry {
    
    namespace _platform_futex_mutex {
        
        // Lowest common denominator platform futex functionality
        
#ifdef __APPLE__
        
        inline void platform_wait_on_address(void* addr, int value) {
            os_sync_wait_on_address(addr, value, sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE);
        }

        inline void platform_wait_on_address_with_timeout(void* addr, int value, uint64_t nanoseconds) {
            os_sync_wait_on_address_with_timeout(addr,
                                                 value,
                                                 sizeof(int),
                                                 OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                 OS_CLOCK_MACH_ABSOLUTE_TIME,
                                                 nanoseconds);
        }

        inline void platform_wake_by_address_any(void* addr) {
            os_sync_wake_by_address_any(addr, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
        }
        
        inline void platform_wake_by_address_all(void* addr) {
            os_sync_wake_by_address_all(addr, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
        }

#endif // __APPLE__
        
#ifdef _WIN32
        
        inline void platform_wait_on_address(void* addr, int value) {
            WaitOnAddress(addr, &value, sizeof(int), 0);
        }

        inline void platform_wait_on_address_with_timeout(void* addr, int value, uint64_t nanoseconds) {
            WaitOnAddress(addr, &value, sizeof(int), (DWORD)(nanoseconds / 1000000));
        }

        inline void platform_wake_by_address_any(void* addr) {
            WakeByAddressSingle(addr);
        }
        
        inline void platform_wake_by_address_all(void* addr) {
            WakeByAddressAll(addr);
        }

#endif // _WIN32
        
#ifdef __linux__
        
        inline void platform_wait_on_address(void* addr, int value) {
            syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, &value, nullptr, nullptr, 0);
        }
        
        inline void platform_wait_on_address_with_timeout(void* addr, int value, uint64_t nanoseconds) {
            struct timespec timeout {
                tv_sec = (time_t)(nanoseconds / 1000000000),
                tv_nsec = (decltype(tv_ns))(nanoseconds % 1000000000),
            };
            syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, &value, &timeout, nullptr, 0);
        }

        
        inline void platform_wake_by_address_all(void* addr) {
            syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
        }
        
        inline void platform_wake_by_address_all(void* addr) {
            syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
        }
        
#endif // __linux__
        
        
        // Credit: Malte Skarupke
        // https://probablydance.com/2020/10/31/using-tla-in-the-real-world-to-understand-a-glibc-bug/
        // Which refs
        // https://locklessinc.com/articles/mutex_cv_futex/
        
        // Dumb mutex of last resort using platform futex.
        // TODO: Spin before waiting
        // TODO: Exchange vs compare exchange?
        
        // Possible uses:
        // Small lock on Linux
        // Condition variable for os_unfair_lock on macos
                
        struct Mutex {
            enum { UNLOCKED, LOCKED, AWAITED };
            std::atomic<int> _state{UNLOCKED};
            
            // This design avoids compare-exchange operations.
            //
            // The interesting case is this ordering:
            //
            // The mutex is AWAITED.  A holds the lock, B is waiting.
            //
            // C performs AWAITED -> LOCKED and branches to the slow path.
            // A performs LOCKED -> UNLOCKED and does not wake B.
            // C performs UNLOCKED -> AWAITED and does not wait.
            //
            // The mutex is AWAITED.  C holds the lock, B is waiting.
            //
            // We are back at the initial state.  We appear to have missed a
            // wakeup for B, but we have only shown that the mutex is unfair,
            // with C having taken B's "turn".
            //
            // Eventually, we will see a different ordering:
            //
            // C performs AWAITED -> LOCKED and branches to the slow path.
            // C performs LOCKED -> AWAITED and waits.
            // A performs AWAITED -> UNLOCKED and wakes any waiter; suppose it wakes B
            // B performs UNLOCKED -> AWAITED and does not wait.
            //
            // The mutex is AWAITED.  B holds the lock, C is waiting.
            //
            // For B to never be woken, the lock must be heavily contended and
            // the wakeup must be flawed.
            //
            // Any thread entering the mutex that sees LOCKED or AWAITED and
            // sets the state UNLOCKED will then enter a loop of setting
            // AWAITED and waiting, always triggering a notifcation: directly
            // when unlock sees AWAITED, or indirectly by forcing the next
            // lock to the slow path.

            // The danger is lost wakeups.
            //
            // AWAITED -> LOCKED always causes AWAITED to be written again.
            // AWAITED -> UNLOCKED always causes a wakeup.
            
            void lock() {
                if (_state.exchange(LOCKED, std::memory_order_acquire) == UNLOCKED)
                    return;
                while (_state.exchange(AWAITED, std::memory_order_acquire) != UNLOCKED)
                    platform_wait_on_address(&_state, AWAITED);
            }
            
            void unlock() {
                if (_state.exchange(UNLOCKED, std::memory_order_release) == AWAITED)
                    platform_wake_by_address_any(&_state);
            }
                        
        };
        
        struct ConditionVariable {
            std::atomic<int> _state{0};
            void wait(std::unique_lock<Mutex>& guard) {
                int old_state = _state.load(std::memory_order_relaxed);
                guard.unlock();
                platform_wait_on_address(&_state, old_state);
                guard.lock();
            }
            void notify_one() {
                _state.fetch_add(1, std::memory_order_relaxed);
                platform_wake_by_address_any(&_state);
            }
            void notify_all() {
                _state.fetch_add(1, std::memory_order_relaxed);
                platform_wake_by_address_all(&_state);
            }
        };
                
    } // namespace
    
    // Platform-specific lightweight locks
    
#ifdef __APPLE__
    struct FastLockable {
        os_unfair_lock _lock = OS_UNFAIR_LOCK_INIT;
        void lock() {
#ifndef NDEBUG
            os_unfair_lock_assert_not_owner(&_lock);
#endif
            os_unfair_lock_lock(&_lock);
        }
        void unlock() {
#ifndef NDEBUG
            os_unfair_lock_assert_owner(&_lock);
#endif
            os_unfair_lock_unlock(&_lock);
        }
        bool try_lock() {
#ifndef NDEBUG
            os_unfair_lock_assert_not_owner(&_lock);
#endif
            return os_unfair_lock_trylock(&_lock);
        }
    };
    
    using FastBasicLockable = FastLockable;
#endif
    
#ifdef _WIN32
    struct FastLockable {
        SRWLOCK _lock = SRWLOCK_INIT;
        void lock() {
            AcquireSRWLockExclusive(&_lock);
        }
        void unlock() {
            ReleaseSRWLockExclusive(&_lock);
        }
        bool try_lock() {
            return AcquireSRWLockExclusive(&_lock);
        }
    };
    using FastBasicLockable = FastLockable;
#endif

    
} // namespace wry

#endif /* mutex_hpp */
