//
//  boxed.hpp
//  client
//
//  Created by Antony Searle on 7/7/2025.
//

#ifndef boxed_hpp
#define boxed_hpp

#include "garbage_collected.hpp"
#include "mutex.hpp"

namespace wry {
    
    template<typename T>
    struct ImmutableBoxed : GarbageCollected {
        
        T data;
        
        explicit ImmutableBoxed(auto&&... args) : data(FORWARD(args)...) {}
        
        virtual ~ImmutableBoxed() {
            printf("%s\n", __PRETTY_FUNCTION__);
        }
        
        virtual void _garbage_collected_scan() const override {
            garbage_collected_enumerate_strong_pointers(data);
        }
        
        static const ImmutableBoxed* make(auto&&... args) {
            return new ImmutableBoxed{T(FORWARD(args)...)};
        }
        
        T copy_inner() const {
            return T{data};
        }
        
        const ImmutableBoxed* copy_with_mutation(auto&& f) const {
            T mutable_copy{data};
            (void) FORWARD(f)(mutable_copy);
            return make(std::move(mutable_copy));
        }
        
    }; // ImmutableBoxed
    
    
    template<typename T>
    struct SynchronizedBoxed : GarbageCollected {
        
        T _data;
        mutable FastBasicLockable _lock;
        
        explicit SynchronizedBoxed(auto&&... args)
        : _data(FORWARD(args)...) {
        }
        
        virtual void _garbage_collected_scan() const override {
            std::unique_lock guard(_lock);
            garbage_collected_enumerate_strong_pointers(_data);
        }
        
        static SynchronizedBoxed* make(auto&&... args) {
            return new SynchronizedBoxed{FORWARD(args)...};
        }
        
        decltype(auto) access(auto&& f) const {
            std::unique_lock guard(_lock);
            return FORWARD(f)(_data);
        }
        
        decltype(auto) access(auto&& f) {
            std::unique_lock guard(_lock);
            return FORWARD(f)(_data);
        }
        
    }; // struct SynchronizedBoxed
    
} // namespace wry

#endif /* boxed_hpp */
