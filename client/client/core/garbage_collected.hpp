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
#include "bump_allocator.hpp"
#include "concepts.hpp"
#include "typeinfo.hpp"
#include "type_traits.hpp"

namespace wry {

    // Mutator interface

    void mutator_pin();
    void mutator_repin();
    void mutator_unpin();

    // Garbage collector interface

    void collector_run_on_this_thread();
    void collector_cancel();
    void collector_register_cycle_callback(uint64_t number_of_cycles,
                                           void* _Nonnull callback) noexcept;


    // Garbage collected base

    struct GarbageCollected;
    void garbage_collected_shade(GarbageCollected const* _Nullable);
    void garbage_collected_scan(GarbageCollected const* _Nullable);


    struct GarbageCollected {
        
        mutable Atomic<uint16_t> _gray;
        mutable uint16_t _black;
        mutable Atomic<int32_t> _count;

#ifndef NDEBUG
        uint16_t _debug_allocation_gray;
        uint16_t _debug_allocation_black;
        uint32_t _debug_allocation_epoch;
#endif

        static void* _Nonnull operator new(std::size_t count);
        static void operator delete(void* _Nullable pointer);

        // Derived objects must be nothrow
        GarbageCollected();
        GarbageCollected(const GarbageCollected&);
        GarbageCollected(GarbageCollected&&);
        virtual ~GarbageCollected() = default;
        GarbageCollected& operator=(const GarbageCollected&);
        GarbageCollected& operator=(GarbageCollected&&);
                
        constexpr std::strong_ordering operator<=>(const GarbageCollected&);
        constexpr bool operator==(const GarbageCollected&);
        
        virtual void _garbage_collected_debug() const = 0;
        virtual void _garbage_collected_scan() const = 0;
        virtual void _garbage_collected_decide_weak(uint16_t mask, uint16_t gray, uint16_t black) const {};

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

    template<typename> struct Root;
    template<typename> struct AtomicScanSlot;
    template<typename> struct AtomicMarkedScanSlot;

} // namespace wry

namespace wry {

    // Cumulative allocation through GarbageCollected::operator new on this
    // thread.  Debugging telemetry; mirrored into the thread's ThreadPublic
    // node at each pin/repin/unpin.  (Extern rather than inline thread_local;
    // see the note on wry::bump::this_thread_state.)
    extern thread_local uint64_t _thread_local_gc_allocated_bytes;
    extern thread_local uint64_t _thread_local_gc_allocated_objects;

    inline void* _Nonnull GarbageCollected::operator new(std::size_t count) {
        _thread_local_gc_allocated_bytes += count;
        ++_thread_local_gc_allocated_objects;
        return calloc(count, 1);
    }
    
    inline void GarbageCollected::operator delete(void* _Nullable pointer) {
        free(pointer);
    }
    
    inline GarbageCollected::GarbageCollected(const GarbageCollected&)
    : GarbageCollected() {
    }
    
    inline GarbageCollected::GarbageCollected(GarbageCollected&&)
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
    debug(GarbageCollected const* _Nullable self) {
        self->_garbage_collected_debug();
    }

    // Provide a no-op scan for basic non-pointer types
    template<Arithmetic T>
    void garbage_collected_scan(T const&) {}
    
    void garbage_collected_scan_weak(GarbageCollected const* _Nullable);

    // Out-of-line cold path for the 0 -> 1 root transition: shades the
    // object (so rooting a white object is k-work that resets the quiet
    // window, exactly like a Yuasa flip) and files it for the collector's
    // root registry via the report channel.
    void _garbage_collected_root_up(GarbageCollected const* _Nonnull);

    // Subtract (increment the multiplicity of) an object from the implicit
    // Roots multiset.

    inline void garbage_collected_roots_add(GarbageCollected const* _Nullable ptr) {
        if (ptr) {
            int32_t before = ptr->_count.fetch_add_relaxed(1);
            assert(before >= 0 && before != INT32_MAX);
            if (before == 0)
                _garbage_collected_root_up(ptr);
        }
    }

    // Subtract (decrement the multiplicity of) an object from the implicit
    // Roots multiset.  The object must be present in the set.

    inline void garbage_collected_roots_subtract(GarbageCollected const* _Nullable ptr) {
        if (ptr) {
            // SAFETY: When the strong count reaches zero we shade the the
            // object, just as when we ovewrite a traced pointer to the object.
            // The lifetime and the ordering of destruction are then established
            // by the epoch system.  There is no prohibition against
            // transitioning between the zero and positive states multiple
            // times--this just means the object is changing between root and
            // child status.
            int32_t before = ptr->_count.fetch_sub_relaxed(1);
            assert(before > 0);
            if (before == 1)
                garbage_collected_shade(ptr);
        }
    }
    
    // Occurances (multiplicity) of an object in the implicit Roots multiset.
    // This value can be changed by another thread at any time and is only for
    // exposition.
    
    inline int32_t garbage_collected_roots_multiplicity(GarbageCollected const * _Nullable ptr) {
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

    template<typename T>
    struct Root<T*> {
        
        T* _Nullable _ptr;

        template<typename U>
        explicit Root(U* _Nullable u)
        : _ptr(u) {
            garbage_collected_roots_add(_ptr);
        }

        template<typename U>
        Root& operator=(U* _Nullable other) {
            garbage_collected_roots_subtract(_ptr);
            _ptr = other;
            garbage_collected_roots_add(_ptr);
            return *this;
        }

        // constexpr so a thread_local Root can be constinit
        constexpr Root() : _ptr(nullptr) {}

        Root(Root const& other)
        : _ptr(other._ptr) {
            garbage_collected_roots_add(_ptr);
        }
        
        Root(Root&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        ~Root() {
            // To fire this assert, a nonempty Root object must have been
            // erroneously placed into a GarbageCollected object.  Roots point
            // into the garbage collected heap from outside it.
            //
            // Also, it is a contradiction for a collection to destroy a root
            // (root vs pointer-to-root distinction?)
            //
            // Destroying an *empty* Root performs no collected-heap action
            // and is permitted on any thread; a thread_local Root that was
            // nulled by thread_public_deregister must be destructible on the
            // collector thread at its exit.
            if (_ptr) {
                assert_this_thread_is_mutator();
                garbage_collected_roots_subtract(_ptr);
            }
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

        Root& operator=(std::nullptr_t) {
            garbage_collected_roots_subtract(_ptr);
            _ptr = nullptr;
            return *this;
        }

        T& operator*() const {
            assert(_ptr);
            return *_ptr;
        }
        
        T* _Nonnull operator->() const {
            assert(_ptr);
            return _ptr;
        }
        
        explicit operator bool() const {
            return (bool)_ptr;
        }
        
        explicit operator T* _Nullable() const {
            return _ptr;
        }
        
        bool operator!() const {
            return !_ptr;
        }

        bool operator==(Root const&) const = default;
        auto operator<=>(Root const&) const = default;

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
    
    // AtomicScanSlot<T> — atomic strong edge to a GC object T.
    //
    // Implements a Yuasa-style snapshot-at-the-beginning (deletion) write
    // barrier: any pointer overwritten by a store/exchange/CAS has its
    // displaced (old) value shaded, so the collector — which may be tracing
    // concurrently and may have observed the old pointer to be reachable but
    // not yet traced it — does not lose track of it.  The store path uses
    // an exchange so it can recover the displaced pointer; the inner
    // ordering is at least acq_rel (release publish + acquire to dereference
    // the displaced pointer for shading).
    //
    // Implementation note: Yuasa's classic formulation is "if the displaced
    // value is white, shade it gray."  We omit the conditional and just
    // unconditionally fetch_or the gray bits onto the displaced node.  Because
    // gray and black both have the gray bit set, OR-ing onto an already-non-
    // white object is idempotent on its color, so the unconditional form is
    // semantically equivalent to Yuasa's conditional check, with a single
    // RMW instead of a load-branch-store.
    //
    // Every load runs at ≥ acquire (so the loaded pointer can be safely
    // dereferenced) and every store publishes with ≥ release.  Names mirror
    // Atomic<T*>; weaker requested orderings are silently strengthened to
    // the minimum that preserves these invariants.
    //
    // For non-GC pointees, use the generic Slot<T*> alias below — it picks
    // a vanilla Atomic<T*> (no barrier) via the slot_for customization
    // point.

    template<typename T>
    struct AtomicScanSlot<T*> {

        // The constraint sits in the constructors rather than the class
        // body so that merely *naming* AtomicScanSlot<T*> (e.g. as
        // the unselected branch of a std::conditional_t over an incomplete
        // T) does not require T to be complete.  The check fires the
        // first time any AtomicScanSlot is actually constructed.

        using value_type = T*;
        static constexpr bool is_always_lock_free = true;

        Atomic<T*> raw;

        constexpr AtomicScanSlot() noexcept : raw{} {
            static_assert(std::is_base_of_v<GarbageCollected, T>);
        }

        // Construction has no displaced pointer to shade — we're going from
        // "no edge exists" to "edge points at desired".  The pointee's
        // reachability is established when the collector eventually traces
        // the parent (which is still being constructed, so not yet reachable).
        explicit constexpr AtomicScanSlot(T* _Nullable desired) noexcept
        : raw(desired) {
            static_assert(std::is_base_of_v<GarbageCollected, T>);
        }

        ~AtomicScanSlot() {
            // AtomicScanSlot lives in a GC-derived parent and is
            // destroyed only by the collector on the collector thread; no
            // barrier is needed (and the pointee may already be destroyed,
            // so loading it would be unsafe).  Trap any other thread.
            assert_this_thread_is_collector();
        }

        AtomicScanSlot(const AtomicScanSlot&) = delete;
        AtomicScanSlot& operator=(const AtomicScanSlot&) = delete;

        // Load: returns the raw pointer.  Inner load runs at ≥ acquire so the
        // caller can dereference what they get back.

#define MAKE_WRY_ATOMIC_GC_LOAD(public_order, internal_order) \
T* _Nullable load_##public_order() const noexcept {\
    return raw.load_##internal_order();\
}

        MAKE_WRY_ATOMIC_GC_LOAD(relaxed, acquire)
        MAKE_WRY_ATOMIC_GC_LOAD(acquire, acquire)
        MAKE_WRY_ATOMIC_GC_LOAD(seq_cst, seq_cst)

        // Non-atomic access — see Atomic<T>::nonatomic_load /
        // nonatomic_store for the contract.  No shade is performed
        // by nonatomic_store: the precondition (object not yet
        // escaped to other threads) means the collector cannot have
        // observed the displaced pointer, so nothing to preserve.
        T* _Nullable nonatomic_load() const noexcept {
            return raw.nonatomic_load();
        }

        void nonatomic_store(T* _Nullable desired) noexcept {
            raw.nonatomic_store(desired);
        }

        // Store: exchange + Yuasa shade of the displaced pointer.  Inner
        // exchange at ≥ acq_rel: release publishes desired, acquire so the
        // shade can dereference the displaced pointer's _gray field.

#define MAKE_WRY_ATOMIC_GC_STORE(public_order, internal_order) \
void store_##public_order(T* _Nullable desired) noexcept {\
    T* _Nullable old = raw.exchange_##internal_order(desired);\
    garbage_collected_shade(old);\
}

        MAKE_WRY_ATOMIC_GC_STORE(relaxed, acq_rel)
        MAKE_WRY_ATOMIC_GC_STORE(release, acq_rel)
        MAKE_WRY_ATOMIC_GC_STORE(seq_cst, seq_cst)

        // Exchange: same mechanics as store but the displaced pointer is
        // also returned to the caller (after being shaded).

#define MAKE_WRY_ATOMIC_GC_EXCHANGE(public_order, internal_order) \
T* _Nullable exchange_##public_order(T* _Nullable desired) noexcept {\
    T* _Nullable old = raw.exchange_##internal_order(desired);\
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
bool compare_exchange_##strength##_##public_succ##_##public_fail(T* _Nullable& expected, T* _Nullable desired) noexcept {\
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

    }; // struct AtomicScanSlot<T*>


    // AtomicMarkedScanSlot<T*> -- AtomicScanSlot plus a Harris-style mark
    // bit packed into the pointer's low bit.
    //
    // The mark conventionally means "the *owner* of this slot is logically
    // deleted"; per Harris, once a slot is marked its pointer is frozen
    // (every mutation CASes against an unmarked expected value, so all
    // fail).  Concurrent lists unlink a marked node by CASing the
    // predecessor's slot past it.
    //
    // GC integration does the heavy lifting that makes Harris lists easy
    // here: the unlink CAS shades the displaced (unlinked) node -- Yuasa,
    // as AtomicScanSlot -- so in-flight iterators holding it, and the
    // frozen next pointers they may traverse through it, stay valid for
    // the rest of the collection cycle.  No hazard pointers, no ABA: the
    // node is reclaimed only when unreachable.
    //
    // Every load runs at >= acquire; the CAS at >= acq_rel with acquire on
    // failure.  Shading on success is unconditional like AtomicScanSlot
    // (shading an unchanged pointer, e.g. when only the mark bit changes,
    // is idempotent-harmless).

    template<typename T>
    struct AtomicMarkedScanSlot<T*> {

        struct MarkedPointer {
            T* _Nullable ptr;
            bool marked;
            bool operator==(MarkedPointer const&) const = default;
        };

        Atomic<uintptr_t> raw;

        static uintptr_t _encode(T* _Nullable p, bool m) {
            return reinterpret_cast<uintptr_t>(p) | uintptr_t{m};
        }

        static MarkedPointer _decode(uintptr_t v) {
            return MarkedPointer{
                reinterpret_cast<T*>(v & ~uintptr_t{1}),
                (bool)(v & 1)
            };
        }

        // Constraint placement per AtomicScanSlot: in the constructor so
        // naming the type does not require T to be complete.
        constexpr AtomicMarkedScanSlot() noexcept : raw{} {
            static_assert(std::is_base_of_v<GarbageCollected, T>);
            static_assert(alignof(T) >= 2);
        }

        ~AtomicMarkedScanSlot() {
            // Lives in a GC-derived owner; destroyed only by the collector.
            assert_this_thread_is_collector();
        }

        AtomicMarkedScanSlot(const AtomicMarkedScanSlot&) = delete;
        AtomicMarkedScanSlot& operator=(const AtomicMarkedScanSlot&) = delete;

        MarkedPointer load_acquire() const noexcept {
            return _decode(raw.load_acquire());
        }

        // Non-atomic access -- pre-publication setup only; see
        // Atomic<T>::nonatomic_store for the contract.
        MarkedPointer nonatomic_load() const noexcept {
            return _decode(raw.nonatomic_load());
        }

        void nonatomic_store(T* _Nullable p, bool m) noexcept {
            raw.nonatomic_store(_encode(p, m));
        }

        // On failure, expected is updated to the observed value.
        bool compare_exchange_strong(MarkedPointer& expected,
                                     MarkedPointer desired) noexcept {
            uintptr_t e = _encode(expected.ptr, expected.marked);
            bool ok = raw.compare_exchange_strong_acq_rel_acquire(
                e, _encode(desired.ptr, desired.marked));
            if (ok)
                garbage_collected_shade(expected.ptr);
            else
                expected = _decode(e);
            return ok;
        }

    }; // struct AtomicMarkedScanSlot<T*>

    // Scan: the mark bit must be stripped before the pointer is reported.
    template<typename T>
    void garbage_collected_scan(AtomicMarkedScanSlot<T*> const& x) {
        garbage_collected_scan(x.load_acquire().ptr);
    }


    struct ScanDiscipline {
        using IntrusiveAllocator = GarbageCollected;
        template<typename T> using Slot = T;
        template<typename T> using AtomicSlot = AtomicScanSlot<T>;
        using InnerDiscipline = ScanDiscipline;
    };

    struct RootDiscipline {
        using IntrusiveAllocator = GarbageCollected;
        template<typename T> using Slot = Root<T>;
        template<typename T> using AtomicSlot = Atomic<Root<T>>;
        using InnerDiscipline = ScanDiscipline;
    };








    // Scan: load-acquire so the recursing scan can safely dereference.
    template<typename T>
    void garbage_collected_scan(AtomicScanSlot<T*> const& x) {
        garbage_collected_scan(x.raw.load_acquire());
    }

    inline void garbage_collected_scan(BumpAllocated const* _Nullable) {
        // no-op
    }

    
    // Weak-registry feed: objects with a nontrivial
    // _garbage_collected_decide_weak register at construction, so the
    // collector's weak-decision walk visits only actual weak holders
    // instead of virtually dispatching on every known object per pass.
    void garbage_collected_register_weak(GarbageCollected const* _Nonnull);

    template<typename T>
    struct WeakHolder : GarbageCollected {

        void _garbage_collected_debug() const override {
            printf("WeakHolder\n");
        }

        enum State { READY, WAS_LOADED, GONE };

        mutable Atomic<State> _state;
        T const* _Nullable _weak;

        explicit WeakHolder(T const* _Nullable weak) : _state{READY}, _weak{weak} {
            garbage_collected_register_weak(this);
        }

        T const* _Nullable mutator_try_upgrade() const {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, WAS_LOADED))
                            return _weak;
                        break;
                    case WAS_LOADED:
                        return _weak;
                    case GONE:
                        return nullptr;
                    default:
                        std::unreachable();
                }
            }
        }

        virtual void _garbage_collected_decide_weak(uint16_t next_delete_mask,
                                                    uint16_t gray_for_marking,
                                                    uint16_t black_for_marking) const override {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        // Not loaded since last decision
                        if (_weak->_black & next_delete_mask) {
                            // Is reachable
                            return;
                        }
                        // Is not reachable, and is about to be deleted
                        // Race to mark it gone, against mutators loading it
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, GONE)) {
                            // Now marked GONE. The weak referent will be
                            // collected next scan, and this object itself is
                            // basically a tombstone entry in the trie.

                            // Race to erase ourself from the trie, against
                            // mutators replacing it.  If the key is never used
                            // again, this is our only opportunity to reclaim
                            // the resources.
                            _heap_string_ctrie_collector_try_erase(this);
                            // TODO: This requires WeakHolder to know how to erase itself, and from what structure; bad coupling
                            return;
                        }
                        // Failed to mark it GONE, spuriously or because a
                        // mutator LOADED it.  Start over.
                        break;
                    case WAS_LOADED:
                        // Was loaded by a mutator since the last decision
                        // Unconditionally change it back to READY, since
                        // mutators will not transition it from this state.
                        _state.store_relaxed(READY);
                        // Revive the referent through the standard shade
                        // channel (we run on the collector thread, which is
                        // pinned with valid colors): the shadelist entry is
                        // re-promoted like any mutator flip, INCLUDING
                        // deferral across bits still in gray warm-up.  The
                        // old direct fetch_or left the referent
                        // gray-not-black for warm-up bits, which nothing
                        // revisits now that the full pass is gone.
                        garbage_collected_shade(_weak);
                        _weak->_black |= black_for_marking;
                        // TODO: This is enough for childess objects.  We need
                        // to put it on the graystack if it has children.  That's
                        // another awkward interface break.
                        return;
                    case GONE:
                        // Was doomed by a previous decision, is already erased
                        // from the trie.  Will be collected soon.  No action
                        // required.
                        return;
                    default:
                        std::unreachable();
                }
            }
        }

        virtual void _garbage_collected_scan() const override {
            // This enumerates *strong* referents, of which there are none.
        }

    };



    // Register a callback (function pointer + user) to be invoked exactly
    // once after `k` full collection cycles have completed since this call.
    // `k == 0` fires immediately on the calling thread.  Otherwise the
    // callback runs on the collector thread and should be cheap and async-
    // safe — typically just `global_work_queue_schedule`.  Thread-safe;
    // multiple callers may register concurrently.




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


    // Gray bits: set by mutator shading (fetch_or) and by the collector.
    // Black bits: written only by the collector after the object has
    // been published to it.  The constructor and deferred-registration
    // path stamp this field while the object is still visible only to
    // the allocating thread, so a plain (mutable) store is race-free in
    // steady state.

} // namespace wry

#endif /* garbage_collected_hpp */
