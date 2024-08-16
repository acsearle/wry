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
#include "type_traits.hpp"

namespace wry::gc {
        
    using hash_t = std::size_t;
    
    // Defines the set of free functions defining the interface of garbage
    // collection participants, and the specific base class for garbage
    // collection objects that implement those methods via virtual dispatch.
    //
    // A typical pattern is that a container will hold and trace garbage
    // collected elements, but not itself be an object; it will be embedded in
    // a larger object.  All of these entities support the object_ interface.
    
    template<typename T>
    concept ObjectTrait = requires(T& ref, const T& cref) {
        
        // not everything is sensibly hashable
        // pointer hash for mutable containers makes no sense
        // pointer identity for strings relies on interning, for bignums also
        // relies on interning
        { object_hash(cref) } -> std::convertible_to<hash_t>;
        
        { object_debug(cref) };
        { object_passivate(ref) };
        { object_shade(cref) };
        { object_trace(cref) };
        { object_trace_weak(cref) };
    };
    
    // from a Rust perspective, to efficiently allow thin pointers + dynamic
    // dispatch we have to have push all traits up to a common base class

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
        static void* operator new[](size_t number_of_bytes) = delete;
        
        // TODO: delete(number_of_bytes) p for logging?
        static void operator delete(void*);
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
