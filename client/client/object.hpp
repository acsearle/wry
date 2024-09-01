//
//  object.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef object_hpp
#define object_hpp

#include "atomic.hpp"
#include "concepts.hpp"
#include "typeinfo.hpp"
#include "type_traits.hpp"

namespace wry::gc {
        
    using hash_t = std::size_t;
    
    struct Value;
    
    // Tricolor abstraction color
    enum class Color {
        WHITE = 0,
        BLACK = 1,
        GRAY  = 2,
        RED   = 3,
    };
    
    struct AtomicEncodedColor {
        
        Atomic<std::underlying_type_t<Color>> _encoded;
        
        AtomicEncodedColor();
        Color load() const;
        bool compare_exchange(Color& expected, Color desired);
        
    };
    
    // TODO: VirtualObject?
    struct Object {
        
        static void* operator new(size_t number_of_bytes);
        static void operator delete(void*);
        
        static void* operator new[](size_t number_of_bytes) = delete;
        static void operator delete[](void*) = delete;
        
        // TODO: is it useful to have a base class above tricolored + sweep?
        mutable AtomicEncodedColor color;
        
        Object();
        Object(const Object&);
        Object(Object&&);
        virtual ~Object() = default;
        Object& operator=(const Object&);
        Object& operator=(Object&&);
                
        virtual std::strong_ordering operator<=>(const Object&) const;
        virtual bool operator==(const Object&) const;
        
        virtual hash_t _object_hash() const;
        virtual void _object_debug() const;
        virtual void _object_shade() const;
        virtual void _object_trace() const;
        virtual void _object_trace_weak() const;
                
        virtual void _object_scan() const = 0;
        virtual Color _object_sweep() const;

        // TODO: is it useful to have a base class above the Value interface?
        virtual Value _value_insert_or_assign(Value key, Value value);
        virtual bool _value_empty() const;
        virtual size_t _value_size() const;
        virtual bool _value_contains(Value key) const;
        virtual Value _value_find(Value key) const;
        virtual Value _value_erase(Value key);
        virtual Value _value_add(Value right) const;
        virtual Value _value_sub(Value right) const;
        virtual Value _value_mul(Value right) const;
        virtual Value _value_div(Value right) const;
        virtual Value _value_mod(Value right) const;
        virtual Value _value_rshift(Value right) const;
        virtual Value _value_lshift(Value right) const;

    }; // struct Object
    
    template<std::derived_from<Object> T> hash_t object_hash(T*const& self);
    template<std::derived_from<Object> T> void object_debug(T*const& self);
    template<std::derived_from<Object> T> void object_passivate(T*& self);
    template<std::derived_from<Object> T> void object_shade(T*const& self);
    template<std::derived_from<Object> T> void object_trace(T*const& self);
    template<std::derived_from<Object> T> void object_trace_weak(T*const& self);
    
    template<typename T> void any_debug(T const& self) {
        std::string_view sv = type_name<T>();
        printf("(%.*s)\n", (int) sv.size(), sv.data());
    }
    
    template<typename T> auto any_read(T const& self) {
        return self;
    }
    
    
    
    
    template<typename T>
    T any_none;
    
    template<typename T>
    inline constexpr T* any_none<T*> = nullptr;
    
} // namespace wry::gc


namespace wry::gc {
    
    // useful defaults
    
    inline void Object::_object_trace_weak() const {
        _object_trace();
    }
        
    inline Color Object::_object_sweep() const {
        return color.load();
    }
    
    
    // implementation of
    
    template<std::derived_from<Object> T>
    hash_t object_hash(T*const& self ) {
        return self->_object_hash();
    }
    
    template<std::derived_from<Object> T>
    void object_debug(T*const& self) {
        if (self) {
            self->_object_debug();
        } else {
            printf("(const Object*)nullptr\n");
        }
    }

    template<std::derived_from<Object> T>
    void object_passivate(T*& self) {
        self = nullptr;
    }

    template<std::derived_from<Object> T>
    void object_shade(T*const& self) {
        if (self)
            self->_object_shade();
    }
    
    template<std::derived_from<Object> T>
    void object_trace(T*const& self) {
        if (self)
            self->_object_trace();
    }
    
    template<std::derived_from<Object> T>
    void object_trace_weak(T*const& self) {
        if (self)
            self->_object_trace_weak();
    }
        
} // namespace wry::gc

#endif /* object_hpp */
