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

namespace wry {
        
    using hash_t = std::size_t;
    
    struct Value;
        
    using Color = uint64_t;
    
    constexpr Color LOW_MASK  = 0x00000000FFFFFFFF;
    constexpr Color HIGH_MASK = 0xFFFFFFFF00000000;
    
    constexpr uint64_t rotate_left(uint64_t x, int y) {
        return __builtin_rotateleft64(x, y);
    }
    
    constexpr uint64_t rotate_right(uint64_t x, int y) {
        return __builtin_rotateright64(x, y);
    }
    
    constexpr bool is_subset_of(Color a, Color b) {
        return !(a & ~b);
    }
    
    constexpr Color are_black(Color color) {
        return color & __builtin_rotateleft64(color, 32);
    }
    
    constexpr Color are_white(Color color) {
        return are_black(~color);
    }
    
    constexpr Color are_grey(Color color) {
        return are_black(color ^ HIGH_MASK);
    }

    
    struct GarbageCollected {
        
        static void* operator new(std::size_t count) {
            return calloc(count, 1);
        }
        
        static void operator delete(void* pointer) {
            free(pointer);
        }

        static void* operator new[](size_t number_of_bytes) = delete;
        static void operator delete[](void*) = delete;
        
        mutable Atomic<Color> _color;
        
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
        virtual void _garbage_collected_trace(void*) const;
        virtual void _garbage_collected_trace_weak(void*) const;
                
        // ENUMERATE_FIELDS calls TRACE on all fields
        // TRACE of a GarbageCollected object adds itself to the child list
        // TRACE of a composite object recurses into its fields
        struct TraceContext;
        virtual void _garbage_collected_enumerate_fields(TraceContext*) const = 0;
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
    
    template<typename T>
    T any_none;
    
    template<typename T>
    inline constexpr T* any_none<T*> = nullptr;
    
} // namespace wry


namespace wry {
    
    // useful defaults
    
    inline void GarbageCollected::_garbage_collected_trace_weak(void* p) const {
        this->_garbage_collected_trace(p);
    }
    
    inline Color GarbageCollected::_garbage_collected_sweep() const {
        return _color.load(Ordering::RELAXED);
    }
    
    
}
 
namespace wry {
    
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
        if (self)
            self->_garbage_collected_shade();
    }
    inline void trace(const GarbageCollected* self, void* p) {
        if (self)
            self->_garbage_collected_trace(p);
    }
    inline void trace_weak(const GarbageCollected* self, void* p) {
        if (self)
            self->_garbage_collected_trace_weak(p);
    }
    
    template<typename T> void any_debug(T const& self) {
        std::string_view sv = name_of<T>;
        printf("(%.*s)\n", (int) sv.size(), sv.data());
    }
    
    template<typename T> auto any_read(T const& self) {
        return self;
    }
    
    template<std::integral T>
    void trace(const T& self, void* p) {
    }
    
    template<typename ForwardIterator, typename Size>
    ForwardIterator trace_n(ForwardIterator first, Size count, void* p) {
        for (; count > 0; (void)++first, --count) {
            trace(*first,p);
        }
        return first;
    }
    
} // namespace wry

#endif /* garbage_collected_hpp */
