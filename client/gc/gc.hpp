//
//  gc.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

#include <cassert>
#include <cinttypes>
#include <cstdint>

#include <atomic>
#include <deque>
#include <vector>

#include "atomic.hpp"
#include "bag.hpp"
#include "utility.hpp"
#include "../client/utility.hpp"

namespace gc {
    
    enum Class {
        CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY,
        CLASS_HEAP_TABLE,
        CLASS_HEAP_STRING,
        CLASS_HEAP_INT64
    };

    enum Color {
        COLOR_WHITE = 0,
        COLOR_BLACK = 1,
        COLOR_GRAY = 2,
        COLOR_RED = 3,
    };
        
    struct Object {
        
        static void* operator new(std::size_t count);
        static void* operator new[](std::size_t count) = delete;

        Class _class;
        mutable Atomic<Color> _gc_color;

        Object() = delete;
        explicit Object(Class class_);
        Object(const Object&);
        Object(Object&&);
        ~Object();
        Object& operator=(const Object&) = delete;
        Object& operator=(Object&&) = delete;
        
    }; // struct Object
    
    Color invert(Color);
    void shade(const Object*);
    void shade(const Object*, const Object*);
    
    void* allocate(std::size_t count);
    
    std::size_t gc_hash(const Object*);

    
    template<typename T> 
    struct Traced;
    
    template<typename T>
    struct Traced<T*> {
        Traced() = default;
        Traced(const Traced& other);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        explicit Traced(T* other);
        explicit Traced(std::nullptr_t);
        Traced& operator=(T* other);
        Traced& operator=(std::nullptr_t);
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        bool operator==(const Traced& other) const;
        auto operator<=>(const Traced& other) const;
        T* get() const;
        Atomic<T*> _ptr;
    };
    
    template<typename T>
    struct Traced<Atomic<T*>> {
        Atomic<T*> _ptr;
        T* load(Order order) const;
        void store(T* desired, Order order);
        T* exchange(T* desired, Order order);
        bool compare_exchange_weak(T*& expected, T* desired, Order success, Order failure);
        bool compare_exchange_strong(T*& expected, T* desired, Order success, Order failure);
    };
    
    
    
    
    
    
    
    
    
    
    
    
    
    template<typename T>
    Traced<T*>::Traced(const Traced& other)
    : Traced(other.get()) {
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(const Traced& other) {
        return operator=(other.get());
    }
    
    template<typename T>
    Traced<T*>::Traced(T* other)
    : _ptr(other) {
    }
    
    template<typename T>
    Traced<T*>::Traced(std::nullptr_t)
    : _ptr(nullptr) {
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(T* other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = _ptr.load(Order::ACQUIRE);
        _ptr.store(other, Order::RELEASE);
        shade(discovered, other);
        return *this;
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = _ptr.load(Order::RELAXED);
        _ptr.store(nullptr, Order::RELAXED);
        shade(discovered);
        return *this;
    }
    
    template<typename T>
    T* Traced<T*>::operator->() const {
        return _ptr.load(Order::RELAXED);
    }
    
    template<typename T>
    bool Traced<T*>::operator!() const {
        return !get();
    }
    
    template<typename T>
    Traced<T*>::operator bool() const {
        return get();
    }
    
    template<typename T>
    Traced<T*>::operator T*() const {
        return get();
    }
    
    template<typename T>
    T& Traced<T*>::operator*() const {
        return *get();
    }
    
    template<typename T>
    bool Traced<T*>::operator==(const Traced& other) const {
        return get() == other.get();
    }
    
    template<typename T>
    auto Traced<T*>::operator<=>(const Traced& other) const {
        return get() <=> other.get();
    }
    
    template<typename T>
    T* Traced<T*>::get() const {
        return _ptr.load(Order::RELAXED);
    }
    
    
    
    template<typename T>
    T* Traced<Atomic<T*>>::load(Order order) const {
        return _ptr.load(order);
    }
        
    template<typename T>
    void Traced<Atomic<T*>>::store(T* desired, Order order) {
        (void) exchange(desired, order);
    }

    template<typename T> [[nodiscard]]
    T* Traced<Atomic<T*>>::exchange(T* desired, Order order) {
        T* discovered = _ptr.exchange(desired, order);
        shade(discovered, desired);
        return discovered;
    }
    
    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Order success, Order failure) {
        return (_ptr.compare_exchange_weak(expected, desired, success, failure)
                && (shade(expected, desired), true));
    }

    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Order success, Order failure) {
        return (_ptr.compare_exchange_strong(expected, desired, success, failure)
                && (shade(expected, desired), true));
    }

    

    
    
    
    
} // namespace gc

#endif /* gc_hpp */
