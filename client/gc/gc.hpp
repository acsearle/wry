//
//  gc.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

#include <cassert>
#include <cinttypes>
#include <cstdint>

#include <atomic>
#include <deque>
#include <vector>

#include "bag.hpp"
#include "utility.hpp"

namespace gc {
    
    // Garbage Collector interface
    //
    // - Mutators are required to
    //   - execute a write barrier
    //   - log new allocations
    //   - periodically handshake with the mutator to
    //     - get new color scheme
    //     - report if there was at least one WHITE -> GRAY write barrier
    //     - report new allocations
    //   - mark any roots
    // - All these mutator actions are lock-free
    //   - the mutator will never wait for the collector
    //   - no GC pause, no stop the world, no stop the mutators in turn
    //   - the mutators can in theory outrun the collector and exhaust memory
    // - Where lock-free data structures are required, a very simple MPSC
    //   stack design is sufficient, implemented inline with minor variations
    //
    // - The system incurs significant overhead:
    //   - Barrier and allocator need access to thread-local state
    //     - Expensive to lookup on some architectures
    //     - Or, annoying to pass everywhere
    //   - All mutable pointers must be
    //     - atomic so the collector can read them
    //     - store-release so the collector can read through them
    //     - write-barrier so reachability is conservative
    //   - The write barrier adds two relaxed compare_exchanges to the object
    //     headers
    //   - Each object must store its color explicitly
    //   - Each object's address is explicitly stored either by a mutator in
    //     its list of recent allocations, or by the collector in its working
    //     list.  Together with color, this is 16 bytes per object of pure
    //     overhead.
    //   - All data structures must be quasi-concurrent so that they can be
    //     traced by the collector well enough to, in combination with the
    //     write barrier, produce a conservative reachability graph.
    //     - For example, for a fixed capacity allocation, we can't atomically
    //       consider both the size and the "back" element it implies; we have
    //       to rely on the immutable capacity, scan slot, and require the
    //       erase operation to leave unused elements in a traceable (preferably
    //       zeroed) state.
    //   - Unreachable objects will survive several rounds of handshakes due to
    //     the conservative nature of the collector; in particular they will
    //     survive the collection they were rendered unreachable during.
    
    // The collector is not lock-free.  It initiates rounds of "handshakes" with
    // the mutators and cannot progress until they have all responded at their
    // leisure.  In particular, it must wait for all mutators to report no GRAY
    // activity before tracing can terminate.   It maintains a list of
    // surviving objects and recent allocations to scan and sweep whenever
    // not waiting on handshakes.
    
    // An important optimization is to mark leaf objects -- objects with no
    // outgoing pointers to other GC objects -- directly to BLACK, skipping
    // the GRAY stage that indicates the collector must scan them.
    
    // Another important optimization is that, when the collector is scanning
    // its worklist of objects for GRAY objects that must be traced, it places
    // those child objects directly in a stack and then immediately traces
    // those, resulting in a depth-first traversal.  Without this optimization,
    // the collector would mark children GRAY and then scan to rediscover them;
    // a singly linked list appearing in reverse order in the worklist would
    // require O(N) scans, i.e. O(NM) operations to fully trace.
    
    

    
    // TODO: Extend interfaces to accept a context to avoids TLS lookup

    using Color = std::intptr_t;

    template<>
    struct Atomic<Color> {
        std::atomic<std::intptr_t> _color;
        explicit Atomic(Color color);
        Color load() const;
        bool compare_exchange_strong(Color& expected, Color desired);
    };
    
    // Fundamental garbage collected thing
    //
    // TODO: Combine Color with something else to avoid overhead

    struct alignas(16) Object {

        Object();
        Object(const Object&);
        virtual ~Object();
        virtual std::size_t gc_bytes() const;
        virtual void gc_enumerate() const;

        // Hash is arguably nothing to do with GC, and should be in HeapValue?
        virtual std::size_t gc_hash() const;

        static void* operator new(std::size_t count);
        static void* operator new[](std::size_t count) = delete;
        mutable Atomic<Color> _gc_color;
        virtual void _gc_shade() const;
        virtual void _gc_trace() const;
        
    }; // struct Object

    // Services
    
    void shade(const Object*);
    void* allocate(std::size_t count);
    void trace(const Object*);
    void _gc_shade_for_leaf(Atomic<Color>* target);
    
    template<typename T> struct Traced;
    
    template<typename T>
    struct Traced<T*> {
        Traced() = default;
        Traced(const Traced& other);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        explicit Traced(T* other);
        explicit Traced(std::nullptr_t);
        Traced& operator=(T* other);
        Traced& operator=(std::nullptr_t);
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        bool operator==(const Traced& other) const;
        auto operator<=>(const Traced& other) const;
        T* get() const;
        std::atomic<T*> _ptr;
    };
    
    template<typename T>
    struct Atomic<Traced<T>> {
        std::atomic<T*> _ptr;
        // ...
    };
    
    
    
    
    
    
    
    
    
    template<typename T>
    [[nodiscard]] T* read_barrier(const std::atomic<T*>* target) {
        return target->load(std::memory_order_relaxed);
    }
    
    template<typename T>
    void write_barrier(std::atomic<T*>* target, T* desired) {
        T* discovered = target->exchange(desired, std::memory_order_release);
        using gc::shade;
        shade(desired);
        shade(discovered);
    }

    template<typename T>
    void write_barrier(std::atomic<T*>* target, std::nullptr_t) {
        T* discovered = target->exchange(nullptr, std::memory_order_release);
        using gc::shade;
        shade(discovered);
    }

    
    
    
    
    
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
    : _ptr(other) {
    }
    
    template<typename T>
    Traced<T*>::Traced(std::nullptr_t)
    : _ptr(nullptr) {
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(T* other) {
        write_barrier(&_ptr, other);
        return *this;
    }
    
    template<typename T>
    Traced<T*>& Traced<T*>::operator=(std::nullptr_t) {
        write_barrier(&_ptr, nullptr);
        return *this;
    }
    
    template<typename T>
    T* Traced<T*>::operator->() const {
        return _ptr.load(std::memory_order_relaxed);
    }
    
    template<typename T>
    bool Traced<T*>::operator!() const {
        return !get();
    }
    
    template<typename T>
    Traced<T*>::operator bool() const {
        return get();
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
        return _ptr.load(std::memory_order_relaxed);
    }
    
    
    
    
    
    
    
    
    
    
} // namespace gc

#endif /* gc_hpp */
