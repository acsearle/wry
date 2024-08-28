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
    struct Scan;
    
    template<PointerConvertibleTo<Object> T>
    struct Scan<T* const> {
        
        T* const _object;
        
        Scan() : _object(nullptr) {}
        Scan(const Scan& other) = default;
        Scan(Scan&& other) = default;
        explicit Scan(auto* const& other) : _object(other) {}
        Scan(std::nullptr_t) : _object(nullptr) {}
        ~Scan() = default;
        Scan& operator=(const Scan& other) = delete;
        Scan& operator=(Scan&& other) = delete;
        
        T* operator->() const { return _object; }
        bool operator!() const { return !_object; }
        explicit operator bool() const { return static_cast<bool>(_object); }
        operator T*() const { return _object; }
        T& operator*() const { assert(_object); return *_object; }
        bool operator==(const Scan& other) const = default;
        std::strong_ordering operator<=>(const Scan& other) const = default;
        bool operator==(auto* const& other) const { return _object == other; }
        bool operator==(std::nullptr_t) const { return _object == nullptr; }
        std::strong_ordering operator<=>(auto* const& other) const { return _object <=> other; }
        std::strong_ordering operator<=>(std::nullptr_t) const { return _object <=> nullptr; }
        
        T* get() const { return _object; }
        void unsafe_set(T*);
        
    }; // Scan<T*const>
    
    template<PointerConvertibleTo<Object> T> size_t object_hash(const Scan<T*const>&);
    template<PointerConvertibleTo<Object> T> void object_debug(const Scan<T*const>&);
    template<PointerConvertibleTo<Object> T> void object_passivate(Scan<T*const>&);
    template<PointerConvertibleTo<Object> T> void object_shade(const Scan<T*const>&);
    template<PointerConvertibleTo<Object> T> void object_trace(const Scan<T*const>&);
    template<PointerConvertibleTo<Object> T> void object_trace_weak(const Scan<T*const>&);

    
    template<std::derived_from<Object> T>
    struct Scan<T*> {
        
        Atomic<T*> _object;
        
        Scan() = default;
        Scan(const Scan& other);
        Scan(Scan&& other);
        explicit Scan(T*const& other);
        Scan(std::nullptr_t);
        ~Scan() = default;
        Scan& operator=(const Scan& other);
        Scan& operator=(Scan&& other);
        Scan& operator=(T*const& other);
        Scan& operator=(std::nullptr_t);
        
        void swap(Scan<T*>& other);
        
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        bool operator==(const Scan& other) const;
        auto operator<=>(const Scan& other) const;
        
        T* get() const;
        T* take();
        
    }; // struct Traced<T*>
    
    template<std::derived_from<Object> T> size_t object_hash(const Scan<T*>&);
    template<std::derived_from<Object> T> void object_debug(const Scan<T*>&);
    template<std::derived_from<Object> T> void object_passivate(Scan<T*>&);
    template<std::derived_from<Object> T> void object_shade(const Scan<T*>&);
    template<std::derived_from<Object> T> void object_trace(const Scan<T*>&);
    template<std::derived_from<Object> T> void object_trace_weak(const Scan<T*>&);
    
    
    
    template<std::derived_from<Object> T>
    struct Scan<Atomic<T*>> {
        
        Atomic<T*> _object;
        
        Scan() = default;
        Scan(const Scan&) = delete;
        explicit Scan(T* object);
        Scan(std::nullptr_t);
        Scan& operator=(const Scan&) = delete;
        
        T* load(Ordering order) const;
        void store(T* desired, Ordering order);
        T* exchange(T* desired, Ordering order);
        bool compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure);
        bool compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure);
        
    }; // struct Traced<Atomic<T*>>
    
    template<std::derived_from<Object> T> size_t object_hash(const Scan<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_debug(const Scan<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_passivate(Scan<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_shade(const Scan<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_trace(const Scan<Atomic<T*>>&);
    template<std::derived_from<Object> T> void object_trace_weak(const Scan<Atomic<T*>>&);
    
    
    
} // namespace wry::gc

namespace wry::gc {
    
    template<PointerConvertibleTo<Object> T> 
    size_t object_hash(const Scan<T*const>& self) {
        return object_hash(self._object);
    }
    
    template<PointerConvertibleTo<Object> T> 
    void object_debug(const Scan<T*const>& self) {
        object_debug(self._object);
    }
    
    template<PointerConvertibleTo<Object> T> 
    void object_passivate(Scan<T*const>& self) {
        // no-op
    }
    
    template<PointerConvertibleTo<Object> T> 
    void object_shade(const Scan<T*const>& self) {
        object_shade(self._object);
    }
    
    template<PointerConvertibleTo<Object> T> 
    void object_trace(const Scan<T*const>& self) {
        object_trace(self._object);
    }
    
    template<PointerConvertibleTo<Object> T> 
    void object_trace_weak(const Scan<T*const>& self) {
        object_trace(self._object);
    }

    
    template<std::derived_from<Object> T>
    Scan<T*>::Scan(const Scan& other)
    : Scan(other.get()) {
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>::Scan(Scan&& other)
    : Scan(other.take()) {
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>& Scan<T*>::operator=(const Scan& other) {
        return operator=(other.get());
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>& Scan<T*>::operator=(Scan&& other) {
        return operator=(other.take());
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>::Scan(T*const& other)
    : _object(other) {
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>::Scan(std::nullptr_t)
    : _object(nullptr) {
    }
    
    template<std::derived_from<Object> T>
    void Scan<T*>::swap(Scan<T*>& other) {
        T* a = get();
        T* b = other.get();
        _object._store(b);
        other._object._store(a);
        object_shade(a);
        object_shade(b);
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>& Scan<T*>::operator=(T*const& other) {
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
    Scan<T*>& Scan<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        object_shade(discovered);
        return *this;
    }
    
    template<std::derived_from<Object> T>
    T* Scan<T*>::operator->() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<Object> T>
    bool Scan<T*>::operator!() const {
        return !get();
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>::operator bool() const {
        return (bool)get();
    }
    
    template<std::derived_from<Object> T>
    Scan<T*>::operator T*() const {
        return get();
    }
    
    template<std::derived_from<Object> T>
    T& Scan<T*>::operator*() const {
        return *get();
    }
    
    template<std::derived_from<Object> T>
    bool Scan<T*>::operator==(const Scan& other) const {
        return get() == other.get();
    }
    
    template<std::derived_from<Object> T>
    auto Scan<T*>::operator<=>(const Scan& other) const {
        return get() <=> other.get();
    }
    
    template<std::derived_from<Object> T>
    T* Scan<T*>::get() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<Object> T>
    T* Scan<T*>::take() {
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        object_shade(discovered);
        return discovered;
    }
    
    
    
    template<std::derived_from<Object> T>
    size_t object_hash(const Scan<T*>& self) {
        object_hash(self.get());
    }
    
    template<std::derived_from<Object> T>
    void object_debug(const Scan<T*>& self) {
        object_trace(self._object.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_passivate(Scan<T*>& self) {
        (void) self.take();
    }
    
    template<std::derived_from<Object> T>
    void object_shade(const Scan<T*>& self) {
        object_shade(self.get());
    }
    
    template<std::derived_from<Object> T>
    void object_trace(const Scan<T*>& self) {
        object_trace(self._object.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace_weak(const Scan<T*>& self) {
        object_trace_weak(self._object.load(Ordering::ACQUIRE));
    }
    
    
    
    template<std::derived_from<Object> T>
    Scan<Atomic<T*>>::Scan(T* object)
    : _object(object) {
    }
    
    template<std::derived_from<Object> T>
    T* Scan<Atomic<T*>>::load(Ordering order) const {
        return _object.load(order);
    }
    
    template<std::derived_from<Object> T>
    void Scan<Atomic<T*>>::store(T* desired, Ordering order) {
        (void) exchange(desired, order);
    }
    
    template<std::derived_from<Object> T>
    T* Scan<Atomic<T*>>::exchange(T* desired, Ordering order) {
        T* discovered = _object.exchange(desired, order);
        object_shade(discovered);
        object_shade(desired);
        return discovered;
    }
    
    template<std::derived_from<Object> T>
    bool Scan<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    template<std::derived_from<Object> T>
    bool Scan<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    
    
    template<std::derived_from<Object> T>
    size_t object_hash(const Scan<Atomic<Atomic<T*>>>& self) {
        object_hash(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_debug(const Scan<Atomic<T*>>& self) {
        object_trace(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_passivate(Scan<Atomic<T*>>& self) {
        // TODO: is it ever correct to passivate an atomic?
        __builtin_trap();
        self.store(nullptr, Ordering::ACQUIRE);
    }
    
    template<std::derived_from<Object> T>
    void object_shade(const Scan<Atomic<T*>>& self) {
        object_shade(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace(const Scan<Atomic<T*>>& self) {
        object_trace(self.load(Ordering::ACQUIRE));
    }
    
    template<std::derived_from<Object> T>
    void object_trace_weak(const Scan<Atomic<T*>>& self) {
        object_trace_weak(self.load(Ordering::ACQUIRE));
    }
    
    
    
    
    
    template<PointerConvertibleTo<Object> T>
    void any_trace(const Scan<T* const>& self) {
        if (self._object)
            self._object->_object_trace();
    }

    template<PointerConvertibleTo<Object> T>
    void any_trace(const Scan<T*>& self) {
        const T* a = self.get();
        if (a)
            a->_object_trace();
    }

    template<PointerConvertibleTo<Object> T>
    void any_trace(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(std::memory_order_acquire);
        if (a)
            a->_object_trace();
    }
    
    
    template<PointerConvertibleTo<Object> T>
    void any_shade(const Scan<T* const>& self) {
        if (self._object)
            self._object->_object_shade();
    }
    
    template<PointerConvertibleTo<Object> T>
    void any_shade(const Scan<T*>& self) {
        const T* a = self.get();
        if (a)
            a->_object_shade();
    }
    
    template<PointerConvertibleTo<Object> T>
    void any_shade(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(std::memory_order_acquire);
        if (a)
            a->_object_shade();
    }
    
    template<typename T>
    T* any_read(const Scan<T* const>& self) {
        return self._object;
    }

    template<typename T>
    T* any_read(const Scan<T*>& self) {
        return self.get();
    }

    template<typename T>
    T* any_read(const Scan<Atomic<T*>>& self) {
        return self.load(std::memory_order_acquire);
    }
    
    template<typename T>
    inline constexpr T* any_none<Scan<T* const>> = nullptr;

    template<typename T>
    inline constexpr T* any_none<Scan<T*>> = nullptr;

    template<typename T>
    inline constexpr T* any_none<Scan<Atomic<T*>>> = nullptr;

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
