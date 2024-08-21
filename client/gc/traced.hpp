//
//  traced.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef traced_hpp
#define traced_hpp

#include "object.hpp"

namespace wry::gc {
    
    template<typename T>
    struct Traced;
    
    template<typename T>
    struct Traced<T* const> {
        
        T* const _object;
        
        Traced() : _object(nullptr) {}
        Traced(const Traced& other) = default;
        Traced(Traced&& other) = default;
        explicit Traced(auto*const& other) : _object(other) {}
        explicit Traced(std::nullptr_t) : _object(nullptr) {}
        ~Traced() = default;
        Traced& operator=(const Traced& other) = delete;
        Traced& operator=(Traced&& other) = delete;
        
        T* operator->() const { return _object; }
        bool operator!() const { return !_object; }
        explicit operator bool() const { return static_cast<bool>(_object); }
        operator T*() const { return _object; }
        T& operator*() const { assert(_object); return *_object; }
        bool operator==(const Traced& other) const = default;
        std::strong_ordering operator<=>(const Traced& other) const = default;
        bool operator==(auto*const& other) const { return _object == other; }
        std::strong_ordering operator<=>(auto*const& other) const { return _object <=> other; }
        bool operator==(std::nullptr_t) { return _object == nullptr; }
        std::strong_ordering operator<=>(std::nullptr_t) { return _object <=> nullptr; }
        
        T* get() const { return _object; }
        
    };
    
    template<std::derived_from<Object> T>
    struct Traced<T*> {
        
        Atomic<T*> _object;
        
        Traced() = default;
        Traced(const Traced& other);
        Traced(Traced&& other);
        explicit Traced(T*const& other);
        explicit Traced(std::nullptr_t);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        Traced& operator=(Traced&& other);
        Traced& operator=(T*const& other);
        Traced& operator=(std::nullptr_t);
        
        void swap(Traced<T*>& other);
        
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        bool operator==(const Traced& other) const;
        auto operator<=>(const Traced& other) const;
        
        T* get() const;
        T* take();
        
    }; // struct Traced<T*>
    
    template<std::derived_from<Object> T> size_t object_hash(const Traced<T*>&);
    template<std::derived_from<Object> T> void object_debug(const Traced<T*>&);
    template<std::derived_from<Object> T> void object_passivate(Traced<T*>&);
    template<std::derived_from<Object> T> void object_shade(const Traced<T*>&);
    template<std::derived_from<Object> T> void object_trace(const Traced<T*>&);
    template<std::derived_from<Object> T> void object_trace_weak(const Traced<T*>&);
    
    
    
    template<std::derived_from<Object> T>
    struct Traced<Atomic<T*>> {
        
        Atomic<T*> _object;
        
        Traced() = default;
        Traced(const Traced&) = delete;
        explicit Traced(T* object);
        explicit Traced(std::nullptr_t);
        Traced& operator=(const Traced&) = delete;
        
        T* load(Ordering order) const;
        void store(T* desired, Ordering order);
        T* exchange(T* desired, Ordering order);
        bool compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure);
        bool compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure);
        
    }; // struct Traced<Atomic<T*>>
    
    template<std::derived_from<Object> T> size_t object_hash(const Traced<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_debug(const Traced<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_passivate(Traced<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_shade(const Traced<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_trace(const Traced<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_trace_weak(const Traced<Atomic<T*>>&);
    
    
    
} // namespace wry::gc

namespace wry::gc {
    
    template<std::derived_from<Object> T>
    Traced<T*>::Traced(const Traced& other)
    : Traced(other.get()) {
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>::Traced(Traced&& other)
    : Traced(other.take()) {
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>& Traced<T*>::operator=(const Traced& other) {
        return operator=(other.get());
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>& Traced<T*>::operator=(Traced&& other) {
        return operator=(other.take());
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>::Traced(T*const& other)
    : _object(other) {
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>::Traced(std::nullptr_t)
    : _object(nullptr) {
    }
    
    template<std::derived_from<Object> T>
    void Traced<T*>::swap(Traced<T*>& other) {
        T* a = get();
        T* b = other.get();
        _object._store(b);
        other._object._store(a);
        object_shade(a);
        object_shade(b);
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>& Traced<T*>::operator=(T*const& other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = get();
        _object.store(other, Ordering::RELEASE);
        object_shade(discovered);
        object_shade(other);
        return *this;
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>& Traced<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        object_shade(discovered);
        return *this;
    }
    
    template<std::derived_from<Object> T>
    T* Traced<T*>::operator->() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<Object> T>
    bool Traced<T*>::operator!() const {
        return !get();
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>::operator bool() const {
        return (bool)get();
    }
    
    template<std::derived_from<Object> T>
    Traced<T*>::operator T*() const {
        return get();
    }
    
    template<std::derived_from<Object> T>
    T& Traced<T*>::operator*() const {
        return *get();
    }
    
    template<std::derived_from<Object> T>
    bool Traced<T*>::operator==(const Traced& other) const {
        return get() == other.get();
    }
    
    template<std::derived_from<Object> T>
    auto Traced<T*>::operator<=>(const Traced& other) const {
        return get() <=> other.get();
    }
    
    template<std::derived_from<Object> T>
    T* Traced<T*>::get() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<Object> T>
    T* Traced<T*>::take() {
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        object_shade(discovered);
        return discovered;
    }
    
    
    
    template<std::derived_from<Object> T>
    size_t object_hash(const Traced<T*>& self) {
        object_hash(self.get());
    }
    
    template<std::derived_from<Object> T>
    void object_debug(const Traced<T*>& self) {
        object_trace(self._object.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_passivate(Traced<T*>& self) {
        (void) self.take();
    }
    
    template<std::derived_from<Object> T>
    void object_shade(const Traced<T*>& self) {
        object_shade(self.get());
    }
    
    template<std::derived_from<Object> T>
    void object_trace(const Traced<T*>& self) {
        object_trace(self._object.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace_weak(const Traced<T*>& self) {
        object_trace_weak(self._object.load(Ordering::ACQUIRE));
    }
    
    
    
    template<std::derived_from<Object> T>
    Traced<Atomic<T*>>::Traced(T* object)
    : _object(object) {
    }
    
    template<std::derived_from<Object> T>
    T* Traced<Atomic<T*>>::load(Ordering order) const {
        return _object.load(order);
    }
    
    template<std::derived_from<Object> T>
    void Traced<Atomic<T*>>::store(T* desired, Ordering order) {
        (void) exchange(desired, order);
    }
    
    template<std::derived_from<Object> T>
    T* Traced<Atomic<T*>>::exchange(T* desired, Ordering order) {
        T* discovered = _object.exchange(desired, order);
        object_shade(discovered);
        object_shade(desired);
        return discovered;
    }
    
    template<std::derived_from<Object> T>
    bool Traced<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    template<std::derived_from<Object> T>
    bool Traced<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    
    
    template<std::derived_from<Object> T>
    size_t object_hash(const Traced<Atomic<Atomic<T*>>>& self) {
        object_hash(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_debug(const Traced<Atomic<T*>>& self) {
        object_trace(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_passivate(Traced<Atomic<T*>>& self) {
        // TODO: is it ever correct to passivate an atomic?
        __builtin_trap();
        self.store(nullptr, Ordering::ACQUIRE);
    }
    
    template<std::derived_from<Object> T>
    void object_shade(const Traced<Atomic<T*>>& self) {
        object_shade(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace(const Traced<Atomic<T*>>& self) {
        object_trace(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace_weak(const Traced<Atomic<T*>>& self) {
        object_trace_weak(self.load(Ordering::ACQUIRE));
    }
    
    
    
} // namespace wry::gc




// Traced objects are mutable and of interest to the garbage collector, so
// they must be atomic.  Traced<...> and Traced<Atomic<...>> provide these
// services.  Both are backed by Atomic<...>.
//
// Traced implements the write barrier required by concurrent
// garbage collection, conservatively shading both the old and new values
// of any store, and thus ensuring that any value seen by the mutator lasts
// at least until the mutator's next handshake.
//
// Traced<...> use the minimal memory Orderings required for
// a "typical" object that is read and written by one mutator thread:
//     - mutator thread loads on the mutator are RELAXED
//     - mutator thread stores are RELEASE
//     - collector thread loads are ACQUIRE
//     - collector thread stores are not permitted
// The collector thread calls only a few methods, notably sweep, trace and
// debug, and these conservatively use ACQUIRE loads.  We allow the user to
// implement sweep and trace with arbitrary code, so this is a
// footgun, but one that ThreadSanitizer seems to be good at detecting.
//
// Traced<Atomic<...>> relies on the user to correctly implement more
// complicated patterns; this usually means ACQUIRE loads everywhere.

#endif /* traced_hpp */
