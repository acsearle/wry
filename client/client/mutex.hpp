//
//  mutex.hpp
//  client
//
//  Created by Antony Searle on 7/7/2025.
//

#ifndef mutex_hpp
#define mutex_hpp

#include <os/lock.h>

#include <mutex>

namespace wry {
    
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
