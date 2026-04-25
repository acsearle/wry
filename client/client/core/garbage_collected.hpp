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
    
    constexpr bool is_subset_of(uint16_t a, uint16_t b) {
        return !(a & ~b);
    }

    namespace detail {

        // Tricolor abstraction, split into two 16-bit words.  Every
        // GarbageCollected object carries an atomic _gray word and a plain
        // _black word; each concurrent collection claims one bit in each:
        //
        //     gray  black   meaning
        //      0      0     k-white   (unreachable candidate)
        //      1      0     k-gray    (reachable, not yet traced)
        //      1      1     k-black   (reachable, traced)
        //      0      1     not produced in steady state
        //
        // Gray bits are set by mutator shading (via fetch_or) and also by
        // the collector.  Black bits are set only by the collector.  Up to
        // 16 concurrent collections can coexist.

        constexpr uint16_t are_black(uint16_t gray, uint16_t black) {
            return gray & black;
        }

        constexpr uint16_t are_gray(uint16_t gray, uint16_t black) {
            return gray & ~black;
        }

        constexpr uint16_t are_white(uint16_t gray, uint16_t black) {
            return ~gray & ~black;
        }

    }
    
    
    
    struct GarbageCollected {
        
        // Gray bits: set by mutator shading (fetch_or) and by the collector.
        mutable Atomic<uint16_t> _gray;

        // Black bits: written only by the collector after the object has
        // been published to it.  The constructor and deferred-registration
        // path stamp this field while the object is still visible only to
        // the allocating thread, so a plain (mutable) store is race-free in
        // steady state.
        mutable uint16_t _black;

        mutable Atomic<int32_t> _count;
        
        uint16_t _debug_allocation_gray;
        uint16_t _debug_allocation_black;
        uint16_t _debug_allocation_epoch;

        // TODO: _gray (16 bits) and _count (32 bits) now fit in a single
        // 64-bit atomic word together with room to spare; _black can remain
        // a separate plain 16-bit field.
        
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
        
        virtual void _garbage_collected_debug() const = 0;
        virtual void _garbage_collected_shade() const;
        virtual void _garbage_collected_scan() const = 0;
        
    }; // struct GarbageCollected
        


    // SHADE can be called by any mutator at any time; it will turn a white
    // object to gray (with those colors as understood by the mutator) and note
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
    // changed from white to gray by the mutator since the last report.  (This
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
            [[maybe_unused]] int32_t before = ptr->_count.fetch_add(1, Ordering::RELAXED);
            // int32_t after = before + 1;
            // printf("%p->_count = (%" PRId32 " -> %" PRId32 ")\n", ptr, before, after);
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
            int32_t before = ptr->_count.fetch_sub(1, Ordering::RELAXED);
            [[maybe_unused]] int32_t after = before - 1;
            // printf("%p->_count = (%" PRId32 " -> %" PRId32 ")\n", ptr, before, after);
            assert(before > 0);
            if (before == 1) {
                ptr->_garbage_collected_shade();
            }
        }
    }
    
    // Occurances (multiplicity) of an object in the implicit Roots multiset.
    // This value can be changed by another thread at any time and is only for
    // exposition.
    
    inline int32_t
    garbage_collected_roots_multiplicity(const GarbageCollected *ptr) {
        return ptr ? ptr->_count.load(Ordering::RELAXED) : 0;
    }
        
} // namespace wry


namespace wry {
    
    // Root keeps its payload in the implicit roots multiset

    // It allows non-traced contexts like stack frames or coroutine frames to
    // keep garbage-collected objects alive.
    
    // SAFETY: The idiomatic reference counted pointer must increment
    // then decrement, but Root can safely enter and leave a zero count
    // state, so long as the calling thread has pinned the epoch.
    
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
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            garbage_collected_roots_add(other._ptr);
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
            garbage_collected_roots_subtract(_ptr);
            _ptr = other._ptr;
            garbage_collected_roots_add(_ptr);
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
            _ptr = std::exchange(other._ptr, nullptr);
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
    
    
    struct BumpAllocated;
    
    inline void garbage_collected_scan(BumpAllocated const* _Nullable) {
        // no-op
    }
    
} // namespace wry

#endif /* garbage_collected_hpp */
