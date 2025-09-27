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
    
    // bit operations
    
    constexpr uint64_t rotate_left(uint64_t x, int y) {
        return __builtin_rotateleft64(x, y);
    }
    
    constexpr uint64_t rotate_right(uint64_t x, int y) {
        return __builtin_rotateright64(x, y);
    }
    
    constexpr bool is_subset_of(uint64_t a, uint64_t b) {
        return !(a & ~b);
    }
    
    namespace detail {
        
        // tricolor abstraction color
        
        using Color = uint64_t;
        
        constexpr Color LOW_MASK  = 0x00000000FFFFFFFF;
        constexpr Color HIGH_MASK = 0xFFFFFFFF00000000;
        
        constexpr Color are_black(Color color) {
            return color & rotate_left(color, 32);
        }
        
        constexpr Color are_grey(Color color) {
            return are_black(color ^ HIGH_MASK);
        }
        
        constexpr Color are_white(Color color) {
            return are_black(~color);
        }
        
    }
    
    
    
    struct GarbageCollected {
        
        mutable Atomic<detail::Color> _color;
        
        static void* operator new(std::size_t count);
        static void operator delete(void* pointer);
        
        GarbageCollected();
        GarbageCollected(const GarbageCollected&);
        GarbageCollected(GarbageCollected&&);
        virtual ~GarbageCollected() = default;
        GarbageCollected& operator=(const GarbageCollected&);
        GarbageCollected& operator=(GarbageCollected&&);
        
        constexpr std::strong_ordering operator<=>(const GarbageCollected&);
        constexpr bool operator==(const GarbageCollected&);
        
        virtual void _garbage_collected_debug() const;
        virtual void _garbage_collected_shade() const;
        virtual void _garbage_collected_scan() const = 0;
        
    }; // struct GarbageCollected
    
    
    auto garbage_collected_shade(const GarbageCollected* ptr) -> void;
    auto garbage_collected_scan(const GarbageCollected* self) -> void;
    
    // Collector
    
    void collector_run_on_this_thread_until(std::chrono::steady_clock::time_point collector_deadline);
    
    // Mutator
    
    void mutator_become_with_name(const char*);
    void mutator_handshake();
    void mutator_resign();
    void mutator_overwrote(const GarbageCollected* old_ptr);
    void mutator_mark_root(const GarbageCollected* root_ptr);

} // namespace wry

namespace wry {
    
        
    inline auto
    GarbageCollected::operator new(std::size_t count) -> void* {
        return calloc(count, 1);
    }
    
    inline auto
    GarbageCollected::operator delete(void* pointer) -> void{
        free(pointer);
    }
    
    inline
    GarbageCollected::GarbageCollected(const GarbageCollected&)
    : GarbageCollected() {
    }
    
    inline
    GarbageCollected::GarbageCollected(GarbageCollected&&)
    : GarbageCollected() {
    }

    inline auto
    GarbageCollected::operator=(const GarbageCollected&) -> GarbageCollected& {
        return *this;
    }
    
    inline auto
    GarbageCollected::operator=(GarbageCollected&&) -> GarbageCollected& {
        return *this;
    }

    
    inline constexpr auto
    GarbageCollected::operator<=>(const GarbageCollected&) -> std::strong_ordering {
        return std::strong_ordering::equivalent;
    }
    
    inline constexpr auto
    GarbageCollected::operator==(const GarbageCollected&) -> bool {
        return true;
    }

    inline auto
    garbage_collected_shade(const GarbageCollected* ptr) -> void {
        if (ptr)
            ptr->_garbage_collected_shade();
    }



    
    
    
    
    
    
    inline void debug(const GarbageCollected* self) {
        self->_garbage_collected_debug();
    }
    
    
    void mutator_shade_root(const GarbageCollected* root);
    

    inline void garbage_collected_passivate(GarbageCollected*& self) {
        self = nullptr;
    }
        
        
    template<typename T> void any_debug(T const& self) {
        std::string_view sv = name_of<T>;
        printf("(%.*s)\n", (int) sv.size(), sv.data());
    }
    
    template<typename T> auto any_read(T const& self) {
        return self;
    }
    
    template<Arithmetic T>
    void garbage_collected_scan(const T& self) {
    }
    
    void garbage_collected_scan_weak(const GarbageCollected* child);

    
    template<typename T>
    T any_none;
    
    template<typename T>
    inline constexpr T* any_none<T*> = nullptr;

    
        
} // namespace wry

#endif /* garbage_collected_hpp */
