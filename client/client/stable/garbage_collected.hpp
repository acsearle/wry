//
//  garbage_collected.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef garbage_collected_hpp
#define garbage_collected_hpp

#include <cinttypes>

#include "assert.hpp"
#include "atomic.hpp"
#include "concepts.hpp"
#include "typeinfo.hpp"
#include "type_traits.hpp"

namespace wry {
    
    // basic interface
        
    struct GarbageCollected;
    
    void garbage_collected_shade(GarbageCollected const*);
    void garbage_collected_scan(GarbageCollected const*);
    
    void collector_run_on_this_thread();
    void collector_cancel();

    void mutator_pin();
    void mutator_repin();
    void mutator_unpin();
    void mutator_overwrote(GarbageCollected const* old_ptr);
    void mutator_mark_root(GarbageCollected const* root_ptr);
    
    
    
    
    
    
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
        mutable Atomic<std::intptr_t> _count;

        // TODO: We can pack the count and the color into a single word
        // Note that the grey bits are only needed when the count is zero
        
        static void* operator new(std::size_t count);
        static void operator delete(void* pointer);
        
        GarbageCollected();
        GarbageCollected(const GarbageCollected&);
        GarbageCollected(GarbageCollected&&);
        virtual ~GarbageCollected() = default;
        GarbageCollected& operator=(const GarbageCollected&);
        GarbageCollected& operator=(GarbageCollected&&);
        
        struct DeferRegistrationTag {};
        explicit GarbageCollected(DeferRegistrationTag);
        void _garbage_collected_complete_deferred_registration() const;
        
        constexpr std::strong_ordering operator<=>(const GarbageCollected&);
        constexpr bool operator==(const GarbageCollected&);
        
        virtual void _garbage_collected_debug() const = 0;
        virtual void _garbage_collected_shade() const;
        virtual void _garbage_collected_scan() const = 0;
        
    }; // struct GarbageCollected
        


    // SHADE can be called by any mutator at any time; it will turn a white
    // object to grey (with those colors as understood by the mutator) and note
    // that the thread has performed such an action, but otherwise have no
    // effect

    // A compound object should ADL-define garbage_collected_shade to
    // recursively call garbage_collected_shade on each of its members if they are
    // potentially of interest to the garbage collector.  The intent is to
    // shade all GarbageCollected pointers within the object, but not to trace
    // them to other objects; that is the job of the Collector.

    // SCAN can only be called by the collector; it will

    // A compound object should ADL-define garbage_collected_scan to recursively
    // call garbage_collected_scan on each of its members if they are
    // potentially of interest to the garbage collector.  The intent is to
    // report all GarbageCollected pointers within the object to the Collector,
    // but not to trace them to other objects; that is the job of the Collector.
    
   
    
    // TODO:
    //
    // Consider maintaining a mutator-local list of the pointers that have been
    // changed from white to grey by the mutator since the last report.  (This
    // is done by the collector we most closely resemble).  This lets us scan
    // less; but since we simultaneously sweep, does it help?

} // namespace wry

namespace wry {
    
        
    inline auto GarbageCollected::
    operator new(std::size_t count) -> void* {
        return calloc(count, 1);
    }
    
    inline auto GarbageCollected::
    operator delete(void* pointer) -> void{
        free(pointer);
    }
    
    inline GarbageCollected::
    GarbageCollected(const GarbageCollected&)
    : GarbageCollected() {
    }
    
    inline GarbageCollected::
    GarbageCollected(GarbageCollected&&)
    : GarbageCollected() {
    }

    inline auto GarbageCollected::
    operator=(const GarbageCollected&) -> GarbageCollected& {
        return *this;
    }
    
    inline auto GarbageCollected::
    operator=(GarbageCollected&&) -> GarbageCollected& {
        return *this;
    }

    
    inline constexpr auto GarbageCollected::
    operator<=>(const GarbageCollected&) -> std::strong_ordering {
        return std::strong_ordering::equivalent;
    }
    
    inline constexpr auto GarbageCollected::
    operator==(const GarbageCollected&) -> bool {
        return true;
    }

    inline void
    garbage_collected_shade(const GarbageCollected* ptr) {
        if (ptr)
            ptr->_garbage_collected_shade();
    }
    
    inline void
    debug(const GarbageCollected* self) {
        self->_garbage_collected_debug();
    }
    
    // Scanning basic types is useless
    
    template<Arithmetic T>
    void
    garbage_collected_scan(T const&) {
    }
    
    void
    garbage_collected_scan_weak(const GarbageCollected*);
    
    // Subtract (increment the multiplicity of) an object from the implicit
    // Roots multiset.

    inline void
    garbage_collected_roots_add(const GarbageCollected* ptr) {
        if (ptr) {
            [[maybe_unused]] std::intptr_t before = ptr->_count.fetch_add(1, Ordering::RELAXED);
            // std::intptr_t after = before + 1;
            // printf("%p->_count = (%" PRIdPTR " -> %" PRIdPTR ")\n", ptr, before, after);
            assert(before >= 0);
        }
    }

    // Subtract (decrement the multiplicity of) an object from the implicit
    // Roots multiset.  The object must be present in the set.

    inline void
    garbage_collected_roots_subtract(const GarbageCollected* ptr) {
        if (ptr) {
            // SAFETY: When the strong count reaches zero we shade the the
            // object, just as when we ovewrite a traced pointer to the object.
            // The lifetime and the ordering of destruction are then established
            // by the epoch system.  There is no prohibition against
            // transitioning between the zero and positive states multiple
            // times--this just means the object is changing between root and
            // child status.
            std::intptr_t before = ptr->_count.fetch_sub(1, Ordering::RELAXED);
            [[maybe_unused]] std::intptr_t after = before - 1;
            // printf("%p->_count = (%" PRIdPTR " -> %" PRIdPTR ")\n", ptr, before, after);
            assert(before > 0);
            if (before == 1) {
                ptr->_garbage_collected_shade();
            }
        }
    }
    
    // Occurances (multiplicity) of an object in the implicit Roots multiset.
    // This value can be changed by another thread at any time and is only for
    // exposition.
    
    inline std::intptr_t
    garbage_collected_roots_multiplicity(const GarbageCollected *ptr) {
        return ptr ? ptr->_count.load(Ordering::RELAXED) : 0;
    }
        
} // namespace wry


namespace wry {
    
    // Root keeps its payload in the implicit roots multiset

    // It allows non-traced contexts like stack frames or coroutine frames to
    // keep garbage-collected objects alive.
    
    // TODO: Root and Edge
    
    template<typename>
    struct Root;
    
    template<typename T>
    struct Root<T*> {
        
        T* _ptr;
                
        Root() : _ptr(nullptr) {}
        
        Root(Root const& other)
        : _ptr(other._ptr) {
            garbage_collected_roots_add(_ptr);
        }
        
        Root(Root&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        ~Root() {
            garbage_collected_roots_subtract(_ptr);
        }
        
        Root& operator=(Root const& other) {
            // SAFETY: Unlike std::shared_ptr, we don't need to alter the
            //
            garbage_collected_roots_add(other._ptr);
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            return *this;
        }
        
        Root& operator=(Root&& other) {
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            other._ptr = nullptr;
            return *this;
        }
        
        bool operator==(Root const&) const = default;
        auto operator<=>(Root const&) const = default;
        
        template<typename U>
        Root(Root<U> const& other)
        : _ptr(other._ptr) {
            garbage_collected_roots_add(_ptr);
        }
        
        template<typename U>
        Root(Root<U>&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        template<typename U>
        Root& operator=(Root<U> const& other) {
            garbage_collected_roots_add(other._ptr);
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            return *this;
        }
        
        template<typename U>
        Root& operator=(Root<U>&& other) {
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            other._ptr = nullptr;
            return *this;
        }
        
        template<typename U>
        explicit Root(U* ptr)
        : _ptr(ptr) {
            garbage_collected_roots_add(ptr);
        }
        
        template<typename U>
        Root& operator=(U* other) {
            garbage_collected_roots_subtract(_ptr);
            _ptr = other;
            garbage_collected_roots_add(_ptr);
            return *this;
        }
        
        T& operator*() const {
            return *_ptr;
        }
        
        T* operator->() const {
            return _ptr;
        }
        
        explicit operator bool() const {
            return (bool)_ptr;
        }
        
        explicit operator T*() const {
            return _ptr;
        }
        
        bool operator!() const {
            return !_ptr;
        }
        
        bool operator==(std::nullptr_t) const {
            return _ptr == nullptr;
        }

    }; // Root<T*>
    
    
    // An Edge must be a field OF a garbage collected object, and point TO
    // a garbage collected object (the degenerate cases of the same object, or
    // null, are allowed)
    
    template<typename>
    struct Edge;
    
    template<typename T>
    struct Edge<T*> {
        
        T* _ptr;
        
        Edge() : _ptr(nullptr) {}
        
        Edge(Edge const& other)
        : _ptr(other._ptr) {
        }
        
        Edge(Edge&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        ~Edge() {
            // Destruction is different from overwriting
        }
        
        Edge& operator=(Edge const& other) {
            garbage_collected_shade(_ptr);
            _ptr = other._ptr;
            return *this;
        }
        
        Edge& operator=(Edge&& other) {
            garbage_collected_shade(_ptr);
            _ptr = other._ptr;
            other._ptr = nullptr;
            return *this;
        }
        
        bool operator==(Edge const&) const = default;
        auto operator<=>(Edge const&) const = default;
        
        template<typename U>
        Edge(Edge<U> const& other)
        : _ptr(other._ptr) {
        }
        
        template<typename U>
        Edge(Edge<U>&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        template<typename U>
        Edge& operator=(Edge<U> const& other) {
            garbage_collected_shade(_ptr);
            _ptr = other._ptr;
            return *this;
        }
        
        template<typename U>
        Edge& operator=(Edge<U>&& other) {
            garbage_collected_shade(_ptr);
            _ptr = other._ptr;
            other._ptr = nullptr;
            return *this;
        }
        
        template<typename U>
        explicit Edge(U* ptr)
        : _ptr(ptr) {
        }
        
        template<typename U>
        Edge& operator=(U* other) {
            garbage_collected_shade(_ptr);
            _ptr = other;
            return *this;
        }
        
        T& operator*() const {
            return *_ptr;
        }
        
        T* operator->() const {
            return _ptr;
        }
        
        explicit operator bool() const {
            return (bool)_ptr;
        }
        
        explicit operator T*() const {
            return _ptr;
        }
        
        bool operator!() const {
            return !_ptr;
        }
        
        bool operator==(std::nullptr_t) const {
            return _ptr == nullptr;
        }
        
    }; // Edge<T*>
    
} // namespace wry

#endif /* garbage_collected_hpp */
