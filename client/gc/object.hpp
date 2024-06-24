//
//  object.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef object_hpp
#define object_hpp

#include "atomic.hpp"
#include "color.hpp"
#include "traced.hpp"
#include "object.hpp"

namespace wry::gc {
    
    /*
    enum struct Class {

        INT64,
        STRING,
        INDIRECT_FIXED_CAPACITY_VALUE_ARRAY,
        ARRAY,
        TABLE,

        CTRIE,
        CTRIE_CNODE,
        CTRIE_INODE,
        CTRIE_LNODE,
        CTRIE_TNODE,
        
    };
     */
    
    struct MainNode;
    struct CNode;
    struct INode;
    struct LNode;
    struct TNode;
    struct HeapString;
    struct Query;
    struct Value;

    struct Object {
        
        static void* operator new(size_t count);
        static void* operator new[](size_t count) = delete;
        static void operator delete(void*);
        static void operator delete[](void*) = delete;
        
        mutable Atomic<Encoded<Color>> color;
        
        Object();
        Object(const Object&);
        Object(Object&&);
        virtual ~Object() = default;
        Object& operator=(const Object&);
        Object& operator=(Object&&);
                
        virtual std::strong_ordering operator<=>(const Object&) const;
        virtual bool operator==(const Object&) const;
        
        virtual void _object_debug() const;
        virtual size_t _object_hash() const;
        virtual void _object_shade() const;
        virtual void _object_scan() const;
        virtual void _object_trace() const;
        virtual void _object_trace_weak() const;
        virtual Color _object_sweep() const;
        
        virtual const Object* _ctrie_resurrect() const;
        virtual const MainNode* _ctrie_toContracted(const MainNode*) const;
        virtual void _ctrie_clean(int level, const INode* parent) const;
        virtual void _ctrie_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const;
        virtual void _ctrie_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const;
        virtual const HeapString* _ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const;
        virtual const HeapString* _ctrie_find_or_emplace2(Query query, int lev, const INode* parent, const INode* i, const CNode* cn, int pos) const;
        virtual Value _ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const;
        virtual void _ctrie_cleanParent3(const INode* p, const INode* i, size_t hc, int lev) const;
        virtual Value _ctrie_erase2(const HeapString* key, int lev, const INode* parent, const INode* i, const CNode* cn, int pos, uint64_t flag) const;

        virtual bool _value_empty() const;
        virtual size_t _value_size() const;
        virtual bool _value_contains(Value key) const;
        virtual Value _value_find(Value key) const;
        virtual Value _value_insert_or_assign(Value key, Value value);
        virtual Value _value_erase(Value key);

        
        
    }; // struct Object
    
    
    void object_debug(const Object*);
    void object_shade(const Object*);
    size_t object_hash(const Object*);
    void object_trace(const Object*);
    void object_trace_weak(const Object*);
    
    inline const Object* ctrie_resurrect(const Object* object) {
        return object->_ctrie_resurrect();
    }
    
    inline const MainNode* ctrie_toContracted(const Object* object, const MainNode* mn) {
        return object->_ctrie_toContracted(mn);
    }
    
    inline void ctrie_clean(const Object* object, int level, const INode* parent) {
        object->_ctrie_clean(level, parent);
    }

    
    
    
    template<typename T>
    struct Traced<T*> {
        
        Atomic<T*> _object;
        
        Traced() = default;
        Traced(const Traced& other);
        explicit Traced(T* other);
        explicit Traced(std::nullptr_t);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        Traced& operator=(T* other);
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
        
        void trace() const;
        
    }; // struct Traced<T*>
        
    template<typename T>
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
        
        void trace() const;
        
    }; // struct Traced<Atomic<T*>>
    
    

    
    
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
    : _object(other) {
    }
    
    template<typename T>
    Traced<T*>::Traced(std::nullptr_t)
    : _object(nullptr) {
    }
    
    template<typename T>
    void Traced<T*>::swap(Traced<T*>& other) {
        T* a = get();
        T* b = other.get();
        (*this) = b;
        other = a;
    }

    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(T* other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = get();
        _object.store(other, Ordering::RELEASE);
        object_shade(discovered);
        object_shade(other);
        return *this;
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        object_shade(discovered);
        return *this;
    }
    
    template<typename T>
    T* Traced<T*>::operator->() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<typename T>
    bool Traced<T*>::operator!() const {
        return !get();
    }
    
    template<typename T>
    Traced<T*>::operator bool() const {
        return (bool)get();
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
        return _object.load(Ordering::RELAXED);
    }
    
    
    
    
    
    
    template<typename T>
    Traced<Atomic<T*>>::Traced(T* object)
    : _object(object) {
    }
    template<typename T>
    T* Traced<Atomic<T*>>::load(Ordering order) const {
        return _object.load(order);
    }
    
    template<typename T>
    void Traced<Atomic<T*>>::store(T* desired, Ordering order) {
        (void) exchange(desired, order);
    }
    
    template<typename T>
    T* Traced<Atomic<T*>>::exchange(T* desired, Ordering order) {
        T* discovered = _object.exchange(desired, order);
        object_shade(discovered);
        object_shade(desired);
        return discovered;
    }
    
    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    template<typename T>
    bool Traced<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            object_shade(expected);
            object_shade(desired);
        }
        return result;
    }
    
    
    template<typename T>
    void Traced<T*>::trace() const {
        object_trace(_object.load(Ordering::ACQUIRE));
    }
    
    template<typename T>
    void Traced<Atomic<T*>>::trace() const {
        object_trace(_object.load(Ordering::ACQUIRE));
    }
    
} // namespace wry::gc

#endif /* object_hpp */
