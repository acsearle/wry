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
        
        // Credit: Malte Skarupke
        // https://probablydance.com/2020/10/31/using-tla-in-the-real-world-to-understand-a-glibc-bug/
        // Which refs
        // https://locklessinc.com/articles/mutex_cv_futex/
        
        // Mutex of last resort using platform futex
        // TODO: Spin before waiting
        // TODO: Exchange vs compare exchange?
        
        // May be useful on Linux as the word-sized lock
        
        struct Mutex {
            enum { UNLOCKED, LOCKED, AWAITED };
            std::atomic<int> _state{UNLOCKED};
            
            void lock() {
                if (_state.exchange(LOCKED, std::memory_order_acquire) == UNLOCKED)
                    return;
                while (_state.exchange(AWAITED, std::memory_order_acquire) != UNLOCKED) {
                    (void) os_sync_wait_on_address(&_state, AWAITED, sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE);
                }
            }
            
            void unlock() {
                if (_state.exchange(UNLOCKED, std::memory_order_release) == AWAITED)
                    os_sync_wake_by_address_any(&_state, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
            }
            
            
        };
        
        struct ConditionVariable {
            std::atomic<int> _state{0};
            void wait(std::unique_lock<Mutex>& guard) {
                int old_state = _state.load(std::memory_order_relaxed);
                guard.unlock();
                (void) os_sync_wait_on_address(&_state, old_state, sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE);
                guard.lock();
            }
            void notify_one() {
                _state.fetch_add(1, std::memory_order_relaxed);
                os_sync_wake_by_address_any(&_state, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
            }
            void notify_all() {
                _state.fetch_add(1, std::memory_order_relaxed);
                os_sync_wake_by_address_all(&_state, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
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
