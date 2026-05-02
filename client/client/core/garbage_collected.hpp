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
            [[maybe_unused]] int32_t before = ptr->_count.fetch_add_relaxed(1);
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
            int32_t before = ptr->_count.fetch_sub_relaxed(1);
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
        return ptr ? ptr->_count.load_relaxed() : 0;
    }
        
} // namespace wry


namespace wry {
    
    // Root keeps its payload in the implicit roots multiset

    // It allows non-traced contexts like stack frames or coroutine frames to
    // keep garbage-collected objects alive.
    
    // SAFETY: The idiomatic reference counted pointer must increment
    // then decrement, but Root can safely enter and leave a zero count
    // state, so long as the calling thread has pinned the epoch.
    
    // Root must never be called on destruction
    
    void assert_this_thread_is_mutator();
    
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
            // To fire this assert, a Root object must have been erroneously
            // placed into a GarbageCollected object.  Roots point into the
            // garbage collected heap from outside it.
            //
            // Also, it is a contradiction for a collection to destroy a root
            // (root vs pointer-to-root distinction?)
            assert_this_thread_is_mutator();
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
    
    
    // Surprisingly straightforward compared to atomic shared_ptr, because we
    // don't need to atomically load-and-increment; the epoch system lets us
    // do our increments and shades any time before the epoch ends (!)
    //
    // The specialization stores the bare T* atomically.  All public methods
    // accept and return Root<T*>, fixing up the implicit-roots-multiset count
    // as if a Root were stored.  The atomic itself owns one count on whatever
    // T* it currently holds (analogous to a Root field).
    //
    // Because we will dereference any pointer we read (to manipulate its
    // _count), every load-side operation runs at memory_order_acquire or
    // stronger; every store-side operation runs at memory_order_release or
    // stronger.  Names mirror Atomic<T*>; weaker requested orderings are
    // silently strengthened to the minimum that lets us safely follow the
    // pointer.

    template<typename T>
    struct Atomic<Root<T*>> {

        using value_type = Root<T*>;
        static constexpr bool is_always_lock_free = true;

        Atomic<T*> raw;

        constexpr Atomic() noexcept : raw{} {}

        explicit Atomic(Root<T*> desired) noexcept
        : raw(std::exchange(desired._ptr, nullptr)) {
            // raw adopts desired's +1; nulling desired prevents its dtor
            // from subtracting it back off again.
        }

        ~Atomic() {
            // Drop our +1 on whatever pointer we currently hold.  Destruction
            // is single-threaded; no concurrent reader can race with us.
            assert_this_thread_is_mutator();
            garbage_collected_roots_subtract(raw.load_relaxed());
        }

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        // Load: returns a Root<T*> that has its own +1.  Inner load runs at
        // ≥ acquire so the caller can dereference what they get back.

#define MAKE_WRY_ATOMIC_ROOT_LOAD(public_order, internal_order) \
Root<T*> load_##public_order() const noexcept {\
    return Root<T*>(raw.load_##internal_order());\
}

        MAKE_WRY_ATOMIC_ROOT_LOAD(relaxed, acquire)
        MAKE_WRY_ATOMIC_ROOT_LOAD(acquire, acquire)
        MAKE_WRY_ATOMIC_ROOT_LOAD(seq_cst, seq_cst)

        // Store: must recover the displaced pointer to subtract its count, so
        // we use exchange internally regardless of the requested name.  Inner
        // exchange runs at ≥ acq_rel: release on the publish side (so other
        // threads see the new pointer with proper happens-before) and acquire
        // on the load side (so we can dereference the displaced pointer to
        // decrement its count).

#define MAKE_WRY_ATOMIC_ROOT_STORE(public_order, internal_order) \
void store_##public_order(Root<T*> desired) noexcept {\
    T* d = std::exchange(desired._ptr, nullptr);\
    T* old = raw.exchange_##internal_order(d);\
    garbage_collected_roots_subtract(old);\
}

        MAKE_WRY_ATOMIC_ROOT_STORE(relaxed, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_STORE(release, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_STORE(seq_cst, seq_cst)

        // Exchange: identical mechanics to store but the displaced pointer's
        // +1 is transferred to the caller's returned Root rather than
        // released.

#define MAKE_WRY_ATOMIC_ROOT_EXCHANGE(public_order, internal_order) \
Root<T*> exchange_##public_order(Root<T*> desired) noexcept {\
    T* d = std::exchange(desired._ptr, nullptr);\
    T* old = raw.exchange_##internal_order(d);\
    /* Adopt old into the returned Root without touching its count: */\
    /* the +1 the atomic was holding is now the +1 the caller holds. */\
    Root<T*> result;\
    result._ptr = old;\
    return result;\
}

        MAKE_WRY_ATOMIC_ROOT_EXCHANGE(relaxed, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_EXCHANGE(acquire, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_EXCHANGE(release, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_EXCHANGE(acq_rel, acq_rel)
        MAKE_WRY_ATOMIC_ROOT_EXCHANGE(seq_cst, seq_cst)

        // Compare-exchange:
        //   On success: the atomic now holds desired's pointer; the +1 that
        //     was on the old pointer (== expected._ptr at success time) has
        //     to be released, and desired's +1 is transferred to the atomic.
        //     The caller's `expected` is unchanged (still owns its +1).
        //   On failure: the actual current pointer is loaded into expected;
        //     expected's old +1 is released and a fresh +1 is taken on the
        //     loaded value.  desired is untouched (its dtor releases its +1).
        //
        // Inner orderings: success must provide ≥ acq_rel (publish desired AND
        // dereference the displaced pointer to subtract its count); failure
        // must provide ≥ acquire (we add a count on whatever was loaded into
        // expected, which dereferences it).  seq_cst on either side stays
        // seq_cst.

#define MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE(strength, public_succ, public_fail, internal_succ, internal_fail) \
bool compare_exchange_##strength##_##public_succ##_##public_fail(Root<T*>& expected, Root<T*> desired) noexcept {\
    T* exp_raw = expected._ptr;\
    bool ok = raw.compare_exchange_##strength##_##internal_succ##_##internal_fail(exp_raw, desired._ptr);\
    if (ok) {\
        garbage_collected_roots_subtract(expected._ptr);\
        desired._ptr = nullptr;\
    } else {\
        expected = exp_raw;\
    }\
    return ok;\
}

#define MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(public_succ, public_fail, internal_succ, internal_fail) \
MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE(weak,   public_succ, public_fail, internal_succ, internal_fail) \
MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE(strong, public_succ, public_fail, internal_succ, internal_fail)

        // Public name mirrors every CAS combination Atomic<T*> exposes; the
        // internal call uses the upgraded orderings (success ≥ acq_rel,
        // failure ≥ acquire), or seq_cst pass-through.
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(relaxed, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(acquire, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(acquire, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(release, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(release, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(acq_rel, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(acq_rel, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(seq_cst, relaxed, seq_cst, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(seq_cst, acquire, seq_cst, acquire)
        MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2(seq_cst, seq_cst, seq_cst, seq_cst)

#undef MAKE_WRY_ATOMIC_ROOT_LOAD
#undef MAKE_WRY_ATOMIC_ROOT_STORE
#undef MAKE_WRY_ATOMIC_ROOT_EXCHANGE
#undef MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE
#undef MAKE_WRY_ATOMIC_ROOT_COMPARE_EXCHANGE2

        // Notify is independent of the Root semantics — it just wakes
        // futex waiters on the underlying word.

        void notify_one() noexcept { raw.notify_one(); }
        void notify_all() noexcept { raw.notify_all(); }

        // TODO: wait variants.  These need to wait until the underlying T*
        // changes from the caller's expected.  Updating expected on wake
        // requires the same -1/+1 dance as compare_exchange's failure path.

    }; // struct Atomic<Root<T*>>

    // Scan an Atomic<Root<T*>> by reading its raw pointer directly — there is
    // no point manufacturing a Root just to scan and then destroy it, which
    // would add then subtract the same count for nothing.  An acquire load
    // gives us the freedom to dereference the pointer if the scan recurses.
    template<typename T>
    void garbage_collected_scan(Atomic<Root<T*>> const& x) {
        garbage_collected_scan(x.raw.load_acquire());
    }

    // Scanning a Root<T*> just scans the underlying pointer.
    template<typename T>
    void garbage_collected_scan(Root<T*> const& x) {
        garbage_collected_scan(x._ptr);
    }
    
    
    void assert_this_thread_is_collector();
    
    // Slot<T*>
    //
    // An atomic strong edge from one garbage-collected object to another.
    // (Subsumes what an "Edge" type would have been: a non-atomic Edge is
    // unnecessary because the collector can't legally read non-atomic mutator
    // writes — that would be a data race.  The atomic version IS what
    // mutators use when they want the collector to be able to follow the
    // pointer concurrently.)
    //
    // Mechanics differ from Atomic<Root<T*>>: there is no implicit-roots
    // multiset count to maintain, because the parent GC object's reachability
    // is what keeps the pointee alive.  What's required instead is the
    // Dijkstra write barrier: any pointer that is overwritten must be shaded,
    // so the collector — which may be tracing concurrently and may have
    // observed the old pointer to be reachable but not yet traced it — does
    // not lose track of it.
    //
    // As with Atomic<Root<T*>>, every load runs at ≥ acquire (so the loaded
    // pointer can be safely dereferenced) and every store runs at ≥ release
    // on the publish side AND ≥ acquire on the displaced-pointer side (so
    // the shade of the old pointer can dereference it).  Names mirror
    // Atomic<T*>; weaker requested orderings are silently strengthened to the
    // minimum that preserves these invariants.

    template<typename>
    struct Slot;

    template<typename T>
    struct Slot<T*> {

        using value_type = T*;
        static constexpr bool is_always_lock_free = true;

        Atomic<T*> raw;

        constexpr Slot() noexcept : raw{} {}

        // Construction has no displaced pointer to shade — we're going from
        // "no edge exists" to "edge points at desired".  The pointee's
        // reachability is established when the collector eventually traces
        // the parent (which is still being constructed, so not yet reachable).
        explicit constexpr Slot(T* desired) noexcept
        : raw(desired) {}

        ~Slot() {
            // Slot should be a field of a GarbageCollected-derived object,
            // and destroyed only by the collector on the collector thread
            
            // Not only do we not need to run the write barrier, there is no
            // write barrier defined for the collector thread, and the pointee
            // may have already been destroyed leaving us with a dangling
            // pointer we must not load.
            
            // The only useful thing we can do is trap destruction outside the
            // collector (the complement of what ~Root asserts)
            assert_this_thread_is_collector();
        }

        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;

        // Load: returns the raw pointer.  Inner load runs at ≥ acquire so the
        // caller can dereference what they get back.

#define MAKE_WRY_ATOMIC_GC_LOAD(public_order, internal_order) \
T* load_##public_order() const noexcept {\
    return raw.load_##internal_order();\
}

        MAKE_WRY_ATOMIC_GC_LOAD(relaxed, acquire)
        MAKE_WRY_ATOMIC_GC_LOAD(acquire, acquire)
        MAKE_WRY_ATOMIC_GC_LOAD(seq_cst, seq_cst)

        // Store: must shade the displaced pointer (Dijkstra barrier), so we
        // use exchange internally to recover it.  Inner exchange runs at
        // ≥ acq_rel: release on the publish side AND acquire on the load
        // side, so the shade of the displaced pointer can read its _gray
        // field.

#define MAKE_WRY_ATOMIC_GC_STORE(public_order, internal_order) \
void store_##public_order(T* desired) noexcept {\
    T* old = raw.exchange_##internal_order(desired);\
    garbage_collected_shade(old);\
}

        MAKE_WRY_ATOMIC_GC_STORE(relaxed, acq_rel)
        MAKE_WRY_ATOMIC_GC_STORE(release, acq_rel)
        MAKE_WRY_ATOMIC_GC_STORE(seq_cst, seq_cst)

        // Exchange: same mechanics as store but the displaced pointer is
        // also returned to the caller (after being shaded).

#define MAKE_WRY_ATOMIC_GC_EXCHANGE(public_order, internal_order) \
T* exchange_##public_order(T* desired) noexcept {\
    T* old = raw.exchange_##internal_order(desired);\
    garbage_collected_shade(old);\
    return old;\
}

        MAKE_WRY_ATOMIC_GC_EXCHANGE(relaxed, acq_rel)
        MAKE_WRY_ATOMIC_GC_EXCHANGE(acquire, acq_rel)
        MAKE_WRY_ATOMIC_GC_EXCHANGE(release, acq_rel)
        MAKE_WRY_ATOMIC_GC_EXCHANGE(acq_rel, acq_rel)
        MAKE_WRY_ATOMIC_GC_EXCHANGE(seq_cst, seq_cst)

        // compare_exchange:
        //   On success: shade the displaced pointer (== expected at success
        //     time).  expected is unchanged.
        //   On failure: expected is updated to the actual current pointer by
        //     the underlying CAS; nothing else to do.
        //
        // Inner orderings: success ≥ acq_rel (publish desired AND dereference
        // displaced for shade); failure ≥ acquire (caller may dereference the
        // updated expected).  seq_cst stays seq_cst.

#define MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE(strength, public_succ, public_fail, internal_succ, internal_fail) \
bool compare_exchange_##strength##_##public_succ##_##public_fail(T*& expected, T* desired) noexcept {\
    bool ok = raw.compare_exchange_##strength##_##internal_succ##_##internal_fail(expected, desired);\
    if (ok)\
        garbage_collected_shade(expected);\
    return ok;\
}

#define MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(public_succ, public_fail, internal_succ, internal_fail) \
MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE(weak,   public_succ, public_fail, internal_succ, internal_fail) \
MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE(strong, public_succ, public_fail, internal_succ, internal_fail)

        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(relaxed, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(acquire, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(acquire, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(release, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(release, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(acq_rel, relaxed, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(acq_rel, acquire, acq_rel, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(seq_cst, relaxed, seq_cst, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(seq_cst, acquire, seq_cst, acquire)
        MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2(seq_cst, seq_cst, seq_cst, seq_cst)

#undef MAKE_WRY_ATOMIC_GC_LOAD
#undef MAKE_WRY_ATOMIC_GC_STORE
#undef MAKE_WRY_ATOMIC_GC_EXCHANGE
#undef MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE
#undef MAKE_WRY_ATOMIC_GC_COMPARE_EXCHANGE2

        void notify_one() noexcept { raw.notify_one(); }
        void notify_all() noexcept { raw.notify_all(); }

        // TODO: wait variants — same shape as elsewhere; left until a
        // concrete need arises.

    }; // struct Slot<T*>

    // Scan: load-acquire so the recursing scan can safely dereference.
    template<typename T>
    void garbage_collected_scan(Slot<T*> const& x) {
        garbage_collected_scan(x.raw.load_acquire());
    }
    
    
    struct BumpAllocated;
    
    inline void garbage_collected_scan(BumpAllocated const* _Nullable) {
        // no-op
    }
    
} // namespace wry

#endif /* garbage_collected_hpp */
