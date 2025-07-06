//
//  garbage_collected.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef garbage_collected_hpp
#define garbage_collected_hpp

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
    struct GarbageCollected {
        
        static void* operator new(size_t number_of_bytes);
        static void operator delete(void*);
        
        static void* operator new[](size_t number_of_bytes) = delete;
        static void operator delete[](void*) = delete;
        
        // TODO: is it useful to have a base class above tricolored + sweep?
        mutable AtomicEncodedColor color;
        
        GarbageCollected();
        GarbageCollected(const GarbageCollected&);
        GarbageCollected(GarbageCollected&&);
        virtual ~GarbageCollected() = default;
        GarbageCollected& operator=(const GarbageCollected&);
        GarbageCollected& operator=(GarbageCollected&&);
                
        virtual std::strong_ordering operator<=>(const GarbageCollected&) const;
        virtual bool operator==(const GarbageCollected&) const;
        
        virtual hash_t _garbage_collected_hash() const;
        virtual void _garbage_collected_debug() const;
        virtual void _garbage_collected_shade() const;
        virtual void _garbage_collected_trace() const;
        virtual void _garbage_collected_trace_weak() const;
                
        virtual void _garbage_collected_scan() const = 0;
        virtual Color _garbage_collected_sweep() const;

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
    
    inline hash_t hash(const GarbageCollected* self) {
        return self->_garbage_collected_hash();
    }
    inline void debug(const GarbageCollected* self) {
        self->_garbage_collected_debug();
    }
    inline void passivate(GarbageCollected*& self) {
        self = nullptr;
    }
    inline void shade(const GarbageCollected* self) {
        self->_garbage_collected_shade();
    }
    inline void trace(const GarbageCollected* self) {
        self->_garbage_collected_trace();
    }
    inline void trace_weak(const GarbageCollected* self) {
        self->_garbage_collected_trace_weak();
    }
    
    template<typename T> void any_debug(T const& self) {
        std::string_view sv = name_of<T>;
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

namespace wry {
    
    using gc::GarbageCollected;
    
}


namespace wry::gc {
    
    // useful defaults
    
    inline void GarbageCollected::_garbage_collected_trace_weak() const {
        _garbage_collected_trace();
    }
    
    inline Color GarbageCollected::_garbage_collected_sweep() const {
        return color.load();
    }
    
    
}
 
namespace wry::orphan {
    
    // implementation of
    
    template<PointerConvertibleTo<GarbageCollected> T>
    gc::hash_t hash(T*const self ) {
        return self->_garbage_collected_hash();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void debug(const T* self) {
        if (self) {
            self->_garbage_collected_debug();
        } else {
            printf("(const GarbageCollected*)nullptr\n");
        }
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void passivate(T* self) {
        self = nullptr;
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void shade(T*const self) {
        if (self)
            self->_garbage_collected_shade();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void trace(T*const self) {
        if (self)
            self->_garbage_collected_trace();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void trace_weak(T*const self) {
        if (self)
            self->_garbage_collected_trace_weak();
    }
    
    template<std::integral T>
    void trace(const T& self) {
    }

} // namespace wry::orphan

#endif /* garbage_collected_hpp */
