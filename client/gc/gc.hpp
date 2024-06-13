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

#include <deque>
#include <vector>

#include "atomic.hpp"
#include "bag.hpp"
#include "utility.hpp"
#include "../client/utility.hpp"

namespace gc {
    
    struct Object;
    template<typename T> struct Traced;

    
    enum Class {
        CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY,
        CLASS_INT64,
        CLASS_STRING,
        CLASS_TABLE,
    };
    

    enum Color {
        COLOR_WHITE = 0,
        COLOR_BLACK = 1,
        COLOR_GRAY = 2,
        COLOR_RED = 3,
    }; // enum Color
    
    Color color_invert(Color);

    
    struct Object {
        
        static void* operator new(std::size_t count);
        static void* operator new[](std::size_t count) = delete;
        
        Class _class;
        mutable Atomic<Color> _color;
        
        Object() = delete;
        Object(const Object&);
        Object(Object&&);
        ~Object() = default;
        Object& operator=(const Object&);
        Object& operator=(Object&&);

        explicit Object(Class class_);
        
        std::strong_ordering operator<=>(const Object&) const;
        bool operator==(const Object&) const;

    }; // struct Object
    
    void object_shade(const Object*);
    void object_trace(const Object*);
    void* object_allocate(size_t count);
    size_t object_hash(const Object*);


    
    template<typename T>
    struct Traced<T*> {

        Atomic<T*> _atomic_object;

        Traced() = default;
        Traced(const Traced& other);
        explicit Traced(T* other);
        explicit Traced(std::nullptr_t);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
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
        
    }; // struct Traced<T*>
    
    template<typename T> void object_trace(const Traced<T*>&);

    
    
    
    
    template<typename T>
    struct Traced<Atomic<T*>> {
        
        Atomic<T*> _atomic_object;
        
        T* load(Order order) const;
        void store(T* desired, Order order);
        T* exchange(T* desired, Order order);
        bool compare_exchange_weak(T*& expected, T* desired, Order success, Order failure);
        bool compare_exchange_strong(T*& expected, T* desired, Order success, Order failure);
        
    }; // struct Traced<Atomic<T*>>
        
    template<typename T> void object_trace(const Traced<Atomic<T*>>&);

    

    
    
    
    
    
    
    
    
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
    : _atomic_object(other) {
    }
    
    template<typename T>
    Traced<T*>::Traced(std::nullptr_t)
    : _atomic_object(nullptr) {
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(T* other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = get();
        _atomic_object.store(other, Order::RELEASE);
        object_shade(discovered);
        object_shade(other);
        return *this;
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _atomic_object.store(nullptr, Order::RELAXED);
        object_shade(discovered);
        return *this;
    }
    
    template<typename T>
    T* Traced<T*>::operator->() const {
        return _atomic_object.load(Order::RELAXED);
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
        return _atomic_object.load(Order::RELAXED);
    }
    
    template<typename T>
    void object_trace(const Traced<T*>& object) {
        object_trace(object._atomic_object.load(Order::ACQUIRE));
    }

    
    
    template<typename T>
    T* Traced<Atomic<T*>>::load(Order order) const {
        return _atomic_object.load(order);
    }
        
    template<typename T>
    void Traced<Atomic<T*>>::store(T* desired, Order order) {
        (void) exchange(desired, order);
    }

    template<typename T>
    T* Traced<Atomic<T*>>::exchange(T* desired, Order order) {
        T* discovered = _atomic_object.exchange(desired, order);
        object_shade(discovered);
        object_shade(desired);
        return discovered;
    }
    
    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Order success, Order failure) {
        bool result = _atomic_object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }

    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Order success, Order failure) {
        bool result = _atomic_object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }

    
    template<typename T>
    void object_trace(const Traced<Atomic<T*>>& object) {
        object_trace(object.load(Order::ACQUIRE));
    }

    

    
    
    
    
} // namespace gc

#endif /* gc_hpp */
