//
//  Scan.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef Scan_hpp
#define Scan_hpp

//#include "adl.hpp"
#include "garbage_collected.hpp"

namespace wry {
    
    // Scan<T> indicates that the payload wants to be scanned by the garbage
    // collector.
    //     T* const - immutable
    //     T* - mutable, must be atomic internally, collector will ACQUIRE
    //     Atomic<T*> - explicitly atomic, multiple mutator threads may access
    
    template<typename T>
    struct Scan;
    
    template<PointerConvertibleTo<GarbageCollected> T>
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
    
    template<std::derived_from<GarbageCollected> T>
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
    
    template<std::derived_from<GarbageCollected> T>
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
    
    
} // namespace wry

namespace wry {
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(const Scan& other)
    : Scan(other.get()) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(Scan&& other)
    : Scan(other.take()) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(const Scan& other) {
        return operator=(other.get());
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(Scan&& other) {
        return operator=(other.take());
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(T*const& other)
    : _object(other) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(std::nullptr_t)
    : _object(nullptr) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    void Scan<T*>::swap(Scan<T*>& other) {
        T* a = get();
        T* b = other.get();
        _object._store(b);
        other._object._store(a);
        garbage_collected_shade(a);
        garbage_collected_shade(b);
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(T*const& other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = get();
        _object.store(other, Ordering::RELEASE);
        garbage_collected_shade(discovered);
        garbage_collected_shade(other);
        return *this;
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        garbage_collected_shade(discovered);
        return *this;
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::operator->() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<T*>::operator!() const {
        return !get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::operator bool() const {
        return (bool)get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::operator T*() const {
        return get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    T& Scan<T*>::operator*() const {
        return *get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<T*>::operator==(const Scan& other) const {
        return get() == other.get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    auto Scan<T*>::operator<=>(const Scan& other) const {
        return get() <=> other.get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::get() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::take() {
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        garbage_collected_shade(discovered);
        return discovered;
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<Atomic<T*>>::Scan(T* object)
    : _object(object) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<Atomic<T*>>::load(Ordering order) const {
        return _object.load(order);
    }
    
    template<std::derived_from<GarbageCollected> T>
    void Scan<Atomic<T*>>::store(T* desired, Ordering order) {
        (void) exchange(desired, order);
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<Atomic<T*>>::exchange(T* desired, Ordering order) {
        T* discovered = _object.exchange(desired, order);
        garbage_collected_shade(discovered);
        garbage_collected_shade(desired);
        return discovered;
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            garbage_collected_shade(expected);
            garbage_collected_shade(desired);
        }
        return result;
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            garbage_collected_shade(expected);
            garbage_collected_shade(desired);
        }
        return result;
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<T* const>& self) {
        garbage_collected_scan(self._object);
    }

    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<T*>& self) {
        garbage_collected_scan(self._object.load(Ordering::ACQUIRE));
    }

    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(Ordering::ACQUIRE);
        garbage_collected_scan(a);
    }
        
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<T* const>& self) {
        if (self._object)
            self._object->_garbage_collected_shade();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<T*>& self) {
        const T* a = self.get();
        if (a)
            a->_garbage_collected_shade();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(Ordering::ACQUIRE);
        if (a)
            a->_garbage_collected_shade();
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
    
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void any_trace_weak(const Scan<T*const>& self) {
        trace_weak(self._object);
    }

        
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_passivate(Scan<T*>& self) {
        self = nullptr;
    }
    
} // namespace wry

#endif /* Scan_hpp */
