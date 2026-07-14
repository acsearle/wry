//
//  garbage_collected.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <mutex>
#include <thread>
#include <queue>
#include <deque>
#include <map>

#include "garbage_collected.hpp"

#include "bag.hpp"
#include "epoch_allocator.hpp"
#include "HeapString.hpp"
#include "stack.hpp"
#include "thread_public.hpp"
#include "utility.hpp"
#include "term.hpp"
#include "inline_ring_buffer.hpp"

#include "test.hpp"

namespace wry::bump {
    
    // TODO: Something weird happened with inline thread_local that forces this
    // back to ye olde extern model
    thread_local State this_thread_state{};
    
}

namespace wry {
    
    // TODO: Combine with is_pinned?
    enum class ThreadMode : uint8_t {
        MUTATOR = 0,
        COLLECTOR = 1,
    };
    
    constinit thread_local ThreadMode _this_thread_mode;
    
    void assert_this_thread_is_mutator() {
        assert(_this_thread_mode == ThreadMode::MUTATOR);
    }
    
    void assert_this_thread_is_collector() {
        assert(_this_thread_mode == ThreadMode::COLLECTOR);
    }


    
    using epoch::Epoch;

    struct bit16_t {
        
        uint16_t raw;
                
        static constexpr bit16_t unit(int k) {
            return bit16_t{(uint16_t)(1 << (k & 0xF))};
        }
        
        constexpr bool operator[](int k) const {
            return raw & (1 << (k & 0xF));
        }
        
        struct reference {
            
            uint16_t& raw;
            uint16_t mask;
            
            constexpr explicit operator bool() const {
                return raw & mask;
            }
            
            constexpr reference& operator=(bool b) {
                if (b) raw |= mask; else raw &= ~mask;
                return *this;
            }
            
            constexpr reference& operator&=(bool b) {
                if (!b) raw &= ~mask;
                return *this;
            }
            constexpr reference& operator^=(bool b) {
                if (b) raw ^= mask;
                return *this;
            }
            constexpr reference& operator|=(bool b) {
                if (b) raw |= mask;
                return *this;
            }
        };
        
        constexpr reference operator[](int k) { return {raw, (uint16_t)(1 << (k & 0xF))}; }
        
        constexpr bit16_t operator~() const { return { (uint16_t)~raw }; }
        constexpr bool operator==(bit16_t const&) const = default;
        constexpr auto operator<=>(bit16_t const&) const = delete;
        
        constexpr bit16_t operator&(bit16_t const& other) const { return { (uint16_t)(raw & other.raw) }; }
        constexpr bit16_t operator^(bit16_t const& other) const { return { (uint16_t)(raw ^ other.raw) }; }
        constexpr bit16_t operator|(bit16_t const& other) const { return { (uint16_t)(raw | other.raw) }; }
        
        constexpr bit16_t& operator&=(bit16_t const& other) { raw &= other.raw; return *this; }
        constexpr bit16_t& operator^=(bit16_t const& other) { raw ^= other.raw; return *this; }
        constexpr bit16_t& operator|=(bit16_t const& other) { raw |= other.raw; return *this; }

        constexpr explicit operator bool() const { return (bool)raw; }
        constexpr bool operator!() const { return !raw; }

    };

    template<typename T>
    struct masked_array {

        T data[16];

        struct reference {
            masked_array* target;
            bit16_t mask;

            reference& operator=(T right) {
                for (int i = 0; i != 16; ++i) {
                    if (mask[i]) {
                        target->data[i] = right;
                    }
                }
                return *this;
            }
        };

        reference operator[](bit16_t mask) {
            return reference{this, mask};
        }

#define X(Y)\
        bit16_t operator Y (T right) const {\
            bit16_t result;\
            for (int i = 0; i != 16; ++i) {\
                result[i] = data[i] Y right;\
            }\
            return result;\
        }
        X(==)
        X(!=)
        X(<)
        X(<=)
        X(>=)
        X(>)
#undef X

    };


    
    
    
    

#pragma mark - Forward declarations

    using namespace detail;

#pragma mark - Global and thread_local variables

    // Thread-locals get initialized in the poisoned state to catch unpinned
    // access

    constinit thread_local uint16_t _thread_local_gray_for_allocation = 0xFFFF;
    constinit thread_local uint16_t _thread_local_black_for_allocation = 0xFFFF;
    constinit thread_local uint16_t _thread_local_gray_did_shade = 0xFFFF;
    // TODO: Poison the bag somehow
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;

    // Shadelist: the objects this thread flipped white->gray (any bit) since
    // its last report.  The fetch_or return value in garbage_collected_shade
    // is the record-once filter -- whoever flips a bit owns the report duty,
    // so an object enters the stream at most once per bit flip and never
    // twice from different threads.  Entries carry no mask: the collector
    // routes each by the object's *current* _gray word, so duplicate or
    // stale entries resolve themselves.
    constinit thread_local Bag<const GarbageCollected*> _thread_local_shaded_objects;

    // Root-registry feed: objects whose root count went 0 -> 1 since the
    // last report.  The transition also shades (see roots_add), so in-cycle
    // root-ups are k-work covered by the quiet window; the registry exists
    // to answer the one question transitions cannot: which objects were
    // ALREADY rooted when a cycle started.
    constinit thread_local Bag<const GarbageCollected*> _thread_local_rooted_objects;

    // Weak-registry feed: weak holders registered at construction.
    constinit thread_local Bag<const GarbageCollected*> _thread_local_weak_registrations;

    // Allocation telemetry (not poisoned: cumulative, valid unpinned)
    constinit thread_local uint64_t _thread_local_gc_allocated_bytes = 0;
    constinit thread_local uint64_t _thread_local_gc_allocated_objects = 0;

    GarbageCollected::GarbageCollected()
    : _gray{_thread_local_gray_for_allocation}
    , _black{_thread_local_black_for_allocation}
    , _count{0}
#ifndef NDEBUG
    , _debug_allocation_gray{_thread_local_gray_for_allocation}
    , _debug_allocation_black{_thread_local_black_for_allocation}
    , _debug_allocation_epoch{epoch::local_state.known.raw}
#endif
    {
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.

        // TODO: We can require non-null
        _thread_local_new_objects.push(this);
    }

    void garbage_collected_shade(GarbageCollected const* ptr) {
        if (ptr) {
            const uint16_t gray = _thread_local_gray_for_allocation;
            const uint16_t before = ptr->_gray.fetch_or_relaxed(gray);
            const uint16_t did_shade = gray & ~before;
            if (did_shade) {
                _thread_local_gray_did_shade |= did_shade;
                _thread_local_shaded_objects.push(ptr);
            }
        }
    }

    void _garbage_collected_root_up(GarbageCollected const* ptr) {
        assert(ptr);
        garbage_collected_shade(ptr);
        _thread_local_rooted_objects.push(ptr);
    }

    void garbage_collected_register_weak(GarbageCollected const* ptr) {
        assert(ptr);
        _thread_local_weak_registrations.push(ptr);
    }

    constinit Stack<GarbageCollected const*> global_children;

    void garbage_collected_scan(GarbageCollected const* child) {
        if (child) {
            global_children.push(child);
        }
    }

    void garbage_collected_scan_weak(GarbageCollected const* child) {
        // Phase 0: weak edges are not traced.  The collector reaches weak
        // referents only via the dedicated WEAK_DECISION pass (Phase 2+); it
        // does not follow them during normal scan, and a strong shade would
        // defeat the whole point of weakness.  See core/docs/ctrie.md.
    }



    struct Report {

        Report* next = nullptr;
        uint16_t gray_did_shade = 0;
        Bag<const GarbageCollected*> allocations;
        Bag<const GarbageCollected*> shaded;
        Bag<const GarbageCollected*> rooted;
        Bag<const GarbageCollected*> weak_registrations;
        Epoch epoch;

    }; // struct Report


    // We expect that these are accessed by each thread on each quiescence,
    // which is a relatively low rate of contention

    struct Color {
        uint16_t gray;
        uint16_t black;
    };
    constinit Atomic<Color> _global_atomic_color_for_allocation = {};
    constinit Atomic<Report*> _global_atomic_reports_head = {};

    void _mutator_publishes_report() {
        Report* desired = new Report{
            .next = nullptr,
            .gray_did_shade = std::exchange(_thread_local_gray_did_shade, 0),
            .allocations = std::move(_thread_local_new_objects),
            .shaded = std::move(_thread_local_shaded_objects),
            .rooted = std::move(_thread_local_rooted_objects),
            .weak_registrations = std::move(_thread_local_weak_registrations),
            .epoch = epoch::local_state.known
        };
        // Publish with release so the collector's acquire-exchange reads the
        // report -- and the _gray words of the objects shaded before it --
        // immediately, with no epoch embargo.
        //
        // `expected` must be a LOCAL, not desired->next itself: the house
        // compare_exchange writes back through `expected` even on SUCCESS
        // (std::atomic writes only on failure), and a post-publication write
        // to desired->next is a plain store racing the collector's immediate
        // acquire-read of that field.  With a local, desired->next is
        // written only before each attempt.
        Report* expected = _global_atomic_reports_head.load_relaxed();
        do {
            desired->next = expected;
        } while (!_global_atomic_reports_head.compare_exchange_weak_release_relaxed(expected,
                                                                                    desired));
        
    }
    
    void _mutator_poison_color() {
        // Put the color in to a (probably) invalid state that will trip the
        // garbage collector
        _thread_local_gray_for_allocation = 0xFFFF;
        _thread_local_black_for_allocation = 0xFFFF;
        _thread_local_gray_did_shade = 0xFFFF;
    }

    
    void _mutator_load_color() {
        // Load the global colors
        Color color = _global_atomic_color_for_allocation.load_relaxed();
        _thread_local_gray_for_allocation = color.gray;
        _thread_local_black_for_allocation = color.black;
        // If the mutator is running k-gray, any allocations will produce gray
        // objects.  We initialize it as such rather than pay a per-allocation
        // cost.
        _thread_local_gray_did_shade = color.gray & ~color.black;
    }
    
    void mutator_pin() {
        // unpinned color state is poisoned
        epoch::pin_this_thread();
        // must load the color state *after* pinning
        _mutator_load_color();
        _thread_public_note_pin();
    }

    void mutator_repin() {
        _mutator_publishes_report();
        // must publish report *before* unpinning
        epoch::repin_this_thread();
        // must load the color state *after* pinning
        _mutator_load_color();
        _thread_public_note_repin();
    }

    void mutator_unpin() {
        // note first: the hook touches this thread's GC-heap node
        _thread_public_note_unpin();
        _mutator_publishes_report();
        // must publish report before unpinning
        epoch::unpin_this_thread();
        // only poison the color state after it has been reported
        _mutator_poison_color();
    }
        
    enum KPhase {
        
        UNUSED,          // Mutators are white.  Collector ignores.
                         // ...at collector's convenience...
        GRAY_PUBLISHED,  // Mutators becoming gray.  Collector shades.
                         // ...when all mutators are gray...
        BLACK_PUBLISHED, // Mutators becoming black.  Collector traces.
                         // ...when no objects are gray...
        WEAK_DECIDING,   // Mutators are black.  Collector decides fate of weak objects
                         // ...when all objects have been visited...
        SWEEPING,        // Mutators are black.  Collector deletes white objects.
                         // ...when all objects have been visited...
        WHITE_PUBLISHED, // Mutators are becoming white.  Collector waits.
                         // ...when all mutators are white...
        CLEARING,        // Mutators are white.  Collector clears bits.
                         // ...when all objects have been visited...
        
    };
    
    const char* _KPhase_names[] = {
        "UNUSED",
        "GRAY_PUBLISHED",
        "BLACK_PUBLISHED",
        "WEAK_DECIDING",
        "SWEEPING",
        "WHITE_PUBLISHED",
        "CLEARING"
    };

    struct KState {
        KPhase kphase;
        Epoch since;
        int scans;
    };
    
    std::array<KState, 16> kstate = {};
    
    
    
    
    struct Collector {

        // Stage-5 cohorts: the known heap, segregated by arrival.  Each
        // receive that carried allocations appends one cohort.  min_epoch is
        // a lower bound on the allocation epoch of every member: a report
        // pinned at H covers allocations since that mutator's previous
        // quiescence, i.e. from epoch H - 1 at the earliest, so the cohort
        // takes min(H) - 1 over the reports merged into it.  A cohort whose
        // min_epoch is at or after a bit's sweep gate (black publish + 2:
        // every mutator pinned from there allocates k-black) contains only
        // k-marked objects and is skipped by k's sweep.  Sweep-visited
        // cohorts fold into the single mature cohort at the front -- their
        // remaining distinction, birth order, is older than every future
        // gate and therefore spent.
        //
        // needs_strip carries stage-5 clearing: when a bit enters CLEARING
        // it is flagged on every cohort that can hold it, the next sweep
        // walk that visits the cohort strips it from the members (mutators
        // cannot shade a CLEARING bit, so the strip races nothing), and the
        // bit recycles when no cohort carries the flag.  Clearing costs no
        // walk of its own: it rides sweep.
        struct Cohort {
            Epoch min_epoch;
            bool mature;
            uint16_t needs_strip;
            Bag<const GarbageCollected*> objects;
        };
        std::deque<Cohort> _cohorts;
        size_t _heap_objects = 0;
        std::array<Epoch, 16> _k_sweep_gate = {};

        // Strip horizon: while k is CLEARING, any cohort whose min_epoch
        // predates this (= k's white publish + 2) may contain k-marked
        // members and must be flagged for stripping.  Consulted at cohort
        // creation, because the one-shot flagging at the WHITE -> CLEARING
        // transition races late reports: a mutator that loaded its colors
        // before the white publish can deliver k-marked allocations up to
        // a couple of epochs afterwards.  (It cannot deliver them later
        // than the recycle check can see: a pre-ack pin blocks the epoch,
        // so its report is received -- and its cohort flagged -- before
        // the try_advance that could retire k, receive running first in
        // the iteration.)
        std::array<Epoch, 16> _k_strip_before = {};

        bit16_t _is_unused = {(uint16_t)0xFFFF};
        bit16_t _is_gray_published = {};
        bit16_t _is_black_published = {};
        bit16_t _is_weak_deciding = {};
        bit16_t _is_sweeping = {};
        bit16_t _is_white_published = {};
        bit16_t _is_clearing = {};


        uint16_t _gray_for_allocation = 0;
        uint16_t _black_for_allocation = 0;
        uint16_t _debug_assert_white;
        uint16_t _debug_assert_nonblack;

        Atomic<bool> _is_canceled;

        Stack<const GarbageCollected*> _graystack;

        // Shaded objects reported by mutators (stage-2 shadelists), spliced
        // from reports and drained into the trace wavefront at the top of
        // each scan.
        Bag<const GarbageCollected*> _shaded_arrivals;

        // Objects whose gray word touches a bit still in gray warm-up: they
        // cannot be blackened for it yet (4.1's no-early-black rule), and
        // with no full pass to rediscover them they wait here; each
        // GRAY -> BLACK transition re-feeds the bag through the arrivals
        // drain.  Re-deferral for a different warm-up bit is fine --
        // promotion is idempotent -- and entries always survive intervening
        // sweeps, because whatever grayed them for the warm-up bit grayed
        // them for every sweeping bit too.
        Bag<const GarbageCollected*> _deferred_warmup;

        // Stage-4 registries, fed from reports; see the walks at the top of
        // collector_scans.  _root_registry holds candidate standing roots
        // (pruned when their count is observed zero -- a re-root files a
        // fresh event).  _weak_registry holds every live weak holder
        // (pruned exactly when the current pass's sweep is about to delete
        // one, so it never dangles).
        Bag<const GarbageCollected*> _root_registry;
        Bag<const GarbageCollected*> _weak_registry;

        // Margin dashboard.  Volumes received from reports since the last
        // scan line, plus pass/cycle accounting: the stability margin is
        // (allocation rate) versus (retirement rate), and passes-per-cycle
        // is the multiplier the redesign is trying to kill.
        size_t _allocated_since_scan = 0;
        size_t _shaded_since_scan = 0;
        size_t _marked_since_line = 0;
        uint64_t _scan_passes = 0;
        std::array<uint64_t, 16> _cycle_pass0 = {};
        std::array<std::chrono::steady_clock::time_point, 16> _cycle_t0 = {};
        
        // Immediate-report bookkeeping (stage 3).
        //
        // _k_last_work[k] is the latest mutator-pinned epoch whose report
        // carried k-work: a flip of some object white->gray on bit k, or --
        // via the did_shade initialization at color load -- the continued
        // existence of a mutator still allocating k-gray.  Epoch is cyclic
        // on timescales far longer than a collection; comparisons use the
        // wrap-aware operators.
        //
        // _passes_since_k_work[k] counts scans completed with no new k-work
        // received; >= 1 means the last-received k-work has been traced to
        // fixpoint (a scan drains the graystack before returning, and
        // reports are received only between scans).
        std::array<Epoch, 16> _k_last_work = {};
        std::array<uint32_t, 16> _passes_since_k_work = {};

        // Cycle-completion counter and pending callback list.  Bumped each
        // time any kbit transitions CLEARING → UNUSED (i.e., one full cycle
        // of that bit completed).  Waiters drain after each bump.
        //
        // Public entry: `collector_register_cycle_callback` (declared in
        // garbage_collected.hpp).  Used to test that a piece of work has
        // had a chance to be observed and acted on by the collector.
        struct CycleWaiter {
            uint64_t target;
            void* callback;
            uint16_t tag;
        };
        std::mutex _cycle_waiters_mutex;
        std::vector<CycleWaiter> _cycle_waiters;

        // TEMP (shutdown cycle-waiter stall investigation): a relaxed mirror of
        // _cycle_waiters.size(), written under _cycle_waiters_mutex, so the
        // collector loop can gate its diagnostic print on "is anything parked
        // on a cycle?" with a single relaxed load instead of taking the mutex
        // every spin.  Plus collector-thread-only throttle state for that print.
        Atomic<size_t> _cycle_waiters_count{0};
        uint64_t _dbg_last_phases = ~uint64_t{0};
        uint64_t _dbg_stall_iters = 0;

        void _on_cycle_started(uint16_t tag) {
            std::scoped_lock guard{_cycle_waiters_mutex};
            for (auto& x : _cycle_waiters)
                x.tag |= tag;
        }

        void _on_cycle_completed(uint16_t tag) {
            std::vector<CycleWaiter> ready;
            {
                std::scoped_lock guard{_cycle_waiters_mutex};
                auto it = _cycle_waiters.begin();
                while (it != _cycle_waiters.end()) {
                    if (tag & it->tag) {
                        it->tag = 0;
                        if (!--it->target) {
                            ready.push_back(std::move(*it));
                            it = _cycle_waiters.erase(it);
                            // TODO: Use the swap-to-end idiom to avoid
                            // O(N^2) reshuffles.
                        }
                    } else {
                        ++it;
                    }
                }
                _cycle_waiters_count.store_relaxed(_cycle_waiters.size());  // TEMP
            }
            for (auto& w : ready)
                global_work_queue_schedule(w.callback);
        }

        ~Collector() {
            for (auto& c : _cohorts)
                c.objects.leak();
            _shaded_arrivals.leak();
            _deferred_warmup.leak();
            _root_registry.leak();
            _weak_registry.leak();
        }

        // Promote an object gray -> black for every bit whose collection may
        // blacken, and enqueue it for tracing if this newly blackened it.
        // The gray word is whatever its writers made it; _black is
        // collector-owned.  _black_for_allocation is disjoint from
        // _is_clearing (a bit is in exactly one phase), so this can neither
        // set nor resurrect a clearing bit.
        void _promote(const GarbageCollected* object) {
            assert(object);
            uint16_t before_gray = object->_gray.load_relaxed();
            uint16_t before_black = object->_black;
            int32_t reference_count = object->_count.load_relaxed();
            violation(object, before_gray, before_black, reference_count);
            uint16_t mark_black = before_gray & _black_for_allocation;
            uint16_t after_black = before_black | mark_black;
            uint16_t did_set_black = ~before_black & after_black;
            if (did_set_black) {
                object->_black = after_black;
                ++_marked_since_line;
                _graystack.push(object);
            }
            // Gray for a bit still warming up: park for re-promotion at
            // that bit's GRAY -> BLACK transition.
            uint16_t warmup = _gray_for_allocation & ~_black_for_allocation;
            if (before_gray & warmup)
                _deferred_warmup.push(object);
        }

        void collector_receives_reports() {
            // Immediate handoff: the mutator's push is a release and this
            // exchange is an acquire, so the report -- and everything the
            // mutator wrote before publishing it, including the _gray words
            // of the objects it shaded and the headers of the objects it
            // allocated -- is readable now.  No embargo: epochs are no
            // longer needed to make report contents visible, only to bound
            // WHEN a mutator could still hold unpublished work (the +2
            // gates in the phase machine).  Because an exchange reads the
            // head's latest modification-order value, one exchange takes
            // every report published so far: "gate, then exchange, then
            // decide" needs no further ordering.
            assert(epoch::local_state.is_pinned);
            Epoch E = epoch::local_state.known;
            Report* head = _global_atomic_reports_head.exchange_acquire(nullptr);
            Cohort incoming{E, false, 0, {}};
            bool any_allocations = false;
            while (head) {
                Epoch H = Epoch{head->epoch};

                // H is the publisher's pinned epoch; concurrently pinned
                // threads span at most one epoch ahead of us.  (No lower
                // bound: reports may have queued across several of our
                // pass-lengthened iterations.)
                assert(H <= E + 1);

                if (!head->allocations.debug_is_empty()) {
                    // The report covers allocations since its publisher's
                    // previous quiescence: epoch H - 1 at the earliest.
                    Epoch lower = H - 1;
                    incoming.min_epoch = any_allocations
                        ? std::min(incoming.min_epoch, lower) : lower;
                    any_allocations = true;
                    size_t n = head->allocations.debug_size();
                    _allocated_since_scan += n;
                    _heap_objects += n;
                    incoming.objects.splice(std::move(head->allocations));
                }
                _shaded_since_scan += head->shaded.debug_size();
                _shaded_arrivals.splice(std::move(head->shaded));
                _root_registry.splice(std::move(head->rooted));
                _weak_registry.splice(std::move(head->weak_registrations));
                for (int k = 0; k != 16; ++k) {
                    uint16_t bit = 1 << k;
                    if (head->gray_did_shade & bit) {
                        _k_last_work[k] = std::max(_k_last_work[k], H);
                        _passes_since_k_work[k] = 0;
                    }
                }
                delete std::exchange(head, head->next);
            }
            if (any_allocations) {
                // Late-report stripping: this cohort may hold members born
                // k-marked by a mutator that had not yet seen k's white
                // publish, arriving after the CLEARING transition's
                // flagging pass already ran.  Flag it now, or the recycle
                // check has nothing to wait on and k reuses the bit under
                // stale marks.
                for (int k = 0; k != 16; ++k) {
                    if (_is_clearing[k] && incoming.min_epoch < _k_strip_before[k])
                        incoming.needs_strip |= (uint16_t)(1 << k);
                }
                // Receive-time promotion: gray-born objects blacken here
                // once their bit may blacken; those born for a bit still
                // warming up park in _deferred_warmup (via _promote) for
                // that bit's GRAY -> BLACK transition.  Everything born
                // after a black-ack is black at birth and no-ops.
                for (const GarbageCollected* object : incoming.objects)
                    _promote(object);
                _cohorts.push_back(std::move(incoming));
            }
        }

        void loop_until_canceled() {
            
            _this_thread_mode = ThreadMode::COLLECTOR;

            mutator_pin();
            thread_public_register("C0");
            assert(epoch::local_state.is_pinned);
            epoch::Epoch epoch_at_last_change = epoch::local_state.known;

            printf("C0: garbage collector starts\n");

            while (!_is_canceled.load_relaxed()) {

                assert(epoch::local_state.is_pinned);
                epoch::Epoch current_epoch = epoch::local_state.known;

                // Receive every iteration (an empty exchange is one atomic):
                // reports are now the work source for the trace wavefront,
                // not just phase bookkeeping.
                collector_receives_reports();

                if (current_epoch != epoch_at_last_change) {

                    try_advance_collection_phases();
                    
                    Color color = {
                        .gray = _gray_for_allocation,
                        .black = _black_for_allocation
                    };
                    _global_atomic_color_for_allocation.store_relaxed(color);

                    epoch_at_last_change = current_epoch;

                    // (kstate[k].scans is no longer bumped here: the phases
                    // that wait on it need the actual work to have run --
                    // WEAK_DECIDING counts trace's weak walks, SWEEPING
                    // counts sweep walks.)
                }

                // TEMP (shutdown cycle-waiter stall investigation): when a
                // coroutine is parked on a collection cycle (WaitForCollection-
                // Cycles in the weak-string / ctrie tests) the collector must
                // drive that cycle to completion itself.  If it can't, this
                // makes the hang legible: the log tail shows the phase set
                // frozen while `waiters` stays nonzero.  Hot path when
                // nothing is parked is a single relaxed load.  Throttled to
                // fire on state change, plus a periodic [STALLED] line if it
                // sits fully frozen.  Remove once the stall is understood.
                if (_cycle_waiters_count.load_relaxed() != 0) {
                    uint64_t phases = 0;
                    for (int k = 0; k != 16; ++k)
                        phases |= (uint64_t)kstate[k].kphase << (k * 3);
                    bool changed = (phases != _dbg_last_phases);
                    bool periodic = !changed
                                 && ((++_dbg_stall_iters & ((1u << 24) - 1)) == 0);
                    if (changed || periodic) {
                        printf("C: waiters=%zu epoch=%04x phases=%016llx\n",
                               _cycle_waiters_count.load_relaxed(),
                               current_epoch.raw,
                               (unsigned long long)phases);
                        // On a full freeze, name the pinned thread(s): a
                        // stuck pinner is the prime suspect.
                        if (periodic)
                            thread_public_debug_dump();
                        _dbg_last_phases = phases;
                        if (changed)
                            _dbg_stall_iters = 0;
                    }
                } else {
                    _dbg_stall_iters = 0;
                }

                assert(epoch::local_state.is_pinned);
                Epoch A{epoch::local_state.known};

                // Trace is O(new work); the sweep walk -- the one remaining
                // heap-proportional operation -- runs only when a bit needs
                // it, over only the cohorts old enough to matter.
                collector_trace();

                if (_is_sweeping.raw)
                    collector_sweep_walk();

                mutator_repin();
                epoch::wait(A);
                assert(epoch::local_state.is_pinned);

            } // while (!_is_cancelled.load_relaxed())

            // Still pinned with valid colors, so we can retire our node
            // (the root drop shades it).  We remain pinned forever after;
            // nobody is left to need the epoch.
            thread_public_deregister();

            // That final shade recorded into this thread's shadelist, and no
            // report will follow -- the collector is exiting, and the process
            // with it.  Leak the record as the cohorts are leaked, or the
            // Bag destructor asserts at thread exit.
            _thread_local_shaded_objects.leak();

        } // void Collector::loop_until_canceled()
        


        void try_advance_collection_phases() {
            
            // Each phase transition asks one of three kinds of question:
            //
            // *Has time passed?* - i.e., have all mutators observed a color
            // publish?  Answered by counting epochs against kstate[k].since.
            // Used by `GRAY_PUBLISHED`, `WHITE_PUBLISHED`.  With immediate
            // (release/acquire) reports there is no separate "finalization"
            // clock: at since+2 every mutator has repinned, its
            // pre-transition report was pushed before that repin, and the
            // per-iteration exchange has therefore already received it.
            //
            // *Has all the work been done?* — i.e., has every known object been
            // visited? Answered by `kstate[k].scans >= 1`. Used by `SWEEPING`,
            // `CLEARING`. Safe because objects we haven't yet seen are
            // guaranteed to be in the target state by the previous phase's
            // invariant.
            //
            // *What did the mutators actually do?* — i.e., has k-work stopped
            // arriving, and has what arrived been traced?  Answered by
            // `_k_last_work` + `_passes_since_k_work`.  Used only by
            // `BLACK_PUBLISHED`, because tracing termination depends on what
            // the mutators wrote, not just on time.
            
            assert(epoch::local_state.is_pinned);
            epoch::Epoch E = epoch::local_state.known;

            bool first = true;
            bool splice_deferred = false;

            for (int k = 0; k != 16; ++k) {
                auto p = UNUSED;
                if (_is_unused[k])
                    p = UNUSED;
                if (_is_gray_published[k])
                    p = GRAY_PUBLISHED;
                if (_is_black_published[k])
                    p = BLACK_PUBLISHED;
                if (_is_weak_deciding[k])
                    p = WEAK_DECIDING;
                if (_is_sweeping[k])
                    p = SWEEPING;
                if (_is_white_published[k])
                    p = WHITE_PUBLISHED;
                if (_is_clearing[k])
                    p = CLEARING;
                kstate[k].kphase = p;
            }

            // Compute transitions
            
            for (int k = 0; k != 16; ++k) {
                uint16_t bit = 1 << k;
                
                switch (kstate[k].kphase) {
                        
                    case UNUSED:
                        // Only start a collection for the first unused bit we
                        // discover.
                        // TODO: We want to keep the collections spread out; not much point
                        // having them bunched up
                        if (!first)
                            break;
                        first = false;
                        kstate[k] = { GRAY_PUBLISHED, E, 0 };
                        // Conservative: treat cycle start as k-work, so the
                        // quiet window cannot open before warm-up completes.
                        _k_last_work[k] = E;
                        _passes_since_k_work[k] = 0;
                        _cycle_pass0[k] = _scan_passes;
                        _cycle_t0[k] = std::chrono::steady_clock::now();
                        _on_cycle_started(bit);
                        break;
                        
                    case GRAY_PUBLISHED:
                        // Wait until all mutators have updated to run k-gray.
                        // We don't need to wait for reports or scans.
                        if (E < kstate[k].since + 2)
                            break;
                        kstate[k] = { BLACK_PUBLISHED, E, 0 };
                        // Sweep gate: every mutator pinned at E + 2 or later
                        // has adopted k-black, so every allocation from
                        // there is k-marked at birth and cohorts wholly
                        // newer than the gate are exempt from k's sweep.
                        _k_sweep_gate[k] = E + 2;
                        // Everything grayed for k during the warm-up --
                        // shades of old objects and gray-born allocations
                        // alike -- was parked in _deferred_warmup by
                        // _promote; re-feed it through the arrivals drain
                        // now that k may blacken (deferred below until the
                        // masks include the new black bit).
                        splice_deferred = true;
                        break;
                        
                    case BLACK_PUBLISHED: {

                        // We can move bit k from TRACING to WEAK_DECIDING
                        // when:
                        //
                        // (1) every mutator has acknowledged k-black -- no
                        //     one still allocates k-gray.  Because a
                        //     mutator's report push precedes its
                        //     color-adopting repin, and we receive every
                        //     iteration, this also means every k-gray
                        //     warm-up allocation is already in the cohorts
                        //     (and was promoted at receive or at this bit's
                        //     warm-up walk);
                        //
                        // (2) a full quiet window has passed since the last
                        //     reported k-work: at E >= _k_last_work[k] + 2,
                        //     the epoch has advanced twice past that work,
                        //     which requires every then-pinned mutator to
                        //     have repinned -- hence reported -- since it,
                        //     so an unreported k-flip cannot exist.  (New
                        //     flips would have re-bumped _k_last_work: a
                        //     mutator that can still reach a k-white object
                        //     contradicts trace completeness, per the
                        //     snapshot induction -- see the docs); and
                        //
                        // (3) at least one trace completed after the last
                        //     k-work was received, so that work has been
                        //     traced to fixpoint (a trace drains the
                        //     graystack before returning), and the standing
                        //     roots have been grayed by the trace's root
                        //     registry walk.
                        //
                        // After this: no k-gray objects exist, no mutator
                        // can produce one, and every reachable object is
                        // k-black.

                        if (E < kstate[k].since + 2)
                            break;
                        if (E < _k_last_work[k] + 2)
                            break;
                        if (_passes_since_k_work[k] < 1)
                            break;

                        kstate[k] = { WEAK_DECIDING, E, 0 };
                    }
                        break; // from switch

                    case WEAK_DECIDING:
                        if (!kstate[k].scans)
                            break;
                        kstate[k] = { SWEEPING, E, 0 };
                        break;

                    case SWEEPING:
                        // Wait for one sweep to complete
                        if (!kstate[k].scans)
                            break;
                        // All k-white objects are deleted
                        // All objects are k-black
                        kstate[k] = { WHITE_PUBLISHED, E, 0 };
                        break;
                        
                    case WHITE_PUBLISHED: {
                        Epoch F = kstate[k].since;
                        // At F+2 every mutator has repinned since the white
                        // publish: no one allocates or shades k any more,
                        // and the final k-black-allocating reports were
                        // pushed before those repins, so the per-iteration
                        // exchange already received them.  k is stable and
                        // may be cleared.
                        if (E < F + 2)
                            break;
                        kstate[k] = { CLEARING, E, 0 };
                        // Flag k for stripping on every cohort that can
                        // carry it: those with members born before the
                        // white-ack.  Cohorts born after it were born
                        // without k, and mutators can no longer shade it.
                        // Cohorts CREATED after this pass are flagged at
                        // receive against the same horizon (late reports).
                        _k_strip_before[k] = F + 2;
                        for (auto& c : _cohorts)
                            if (c.mature || c.min_epoch < F + 2)
                                c.needs_strip |= bit;
                    } break;

                    case CLEARING: {
                        // Clearing rides sweep: k has been stripped from a
                        // cohort's members when the flag is gone.  k
                        // recycles when no cohort carries it -- at most
                        // about one further cycle, since every flagged
                        // cohort is older than the next sweep's gate.
                        bool pending = false;
                        for (auto& c : _cohorts)
                            if (c.needs_strip & bit) {
                                pending = true;
                                break;
                            }
                        if (pending)
                            break;
                        kstate[k] = { UNUSED, E, 0 };
                        printf("C0: k=%d cycle complete: iters=%llu in %.3gs\n",
                               k,
                               (unsigned long long)(_scan_passes - _cycle_pass0[k]),
                               std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - _cycle_t0[k]).count());
                        _on_cycle_completed(bit);
                    } break;
                        
                } // switch kphase
                
            } // for k

            // TODO: Rather than writing everywhere, we can probably filter with
            // a mask, and that mask is just black_for_allocation
            
            // Derive bitmasks.

            for (int k = 0; k != 16; ++k) {
                auto p = kstate[k].kphase;
                _is_unused[k] = p == UNUSED;
                _is_gray_published[k] = p == GRAY_PUBLISHED;
                _is_black_published[k] = p == BLACK_PUBLISHED;
                _is_weak_deciding[k] = p == WEAK_DECIDING;
                _is_sweeping[k] = p == SWEEPING;
                _is_white_published[k] = p == WHITE_PUBLISHED;
                _is_clearing[k] = p == CLEARING;
            }

            _black_for_allocation = (_is_black_published | _is_weak_deciding | _is_sweeping).raw;
            _gray_for_allocation = (_is_gray_published | _is_black_published | _is_weak_deciding | _is_sweeping).raw;
            _debug_assert_white = _is_unused.raw;
            _debug_assert_nonblack = (_is_unused | _is_gray_published).raw;

            // Deferred from the GRAY -> BLACK transition, after the masks
            // above include the new black bit: re-feed the warm-up's parked
            // grays through the arrivals drain.  This iteration's
            // collector_trace promotes and traces them (and re-parks any
            // that also touch a bit still warming up).
            if (splice_deferred)
                _shaded_arrivals.splice(std::move(_deferred_warmup));

        } // void Collector::try_advance_collection_phases()

        
        void violation(GarbageCollected const* object, uint16_t gray, uint16_t black, int32_t count) {
#ifndef NDEBUG
            uint16_t a = black & ~gray;
            uint16_t b = (gray | black) & _debug_assert_white;
            uint16_t c = black & _debug_assert_nonblack;
            uint16_t d = (gray ^ black) & _is_sweeping.raw;

            if (!(a | b | c | d))
                return;
                                    
            if (a) printf("     RED %04x\n", a);
            if (b) printf("NONWHITE %04x\n", b);
            if (c) printf("   BLACK %04x\n", c);
            if (d) printf("    GRAY %04x\n", d);

            printf(" -- INVARIANT VIOLATED -- \n");
            printf("object->_gray  %04x\n", gray);
            printf("        _black %04x\n", object->_black);
            printf("        _count %04x\n", count);
            object->_garbage_collected_debug();
            
            bool is_pinned = epoch::local_state.is_pinned;
            epoch::Epoch E = epoch::local_state.known;
            
            printf("%s epoch %04x\n", is_pinned ? "In" : "After", E.raw);

            
            printf("states [    ] phase/since/scans \"name\"\n");
            for (int k = 0; k != 16; ++k) {
                uint16_t bit = 1 << k;
                printf("       [%04x] %d/%04x/%d \"%s\"\n",
                       bit,
                       kstate[k].kphase,
                       kstate[k].since.raw,
                       kstate[k].scans,
                       _KPhase_names[kstate[k].kphase]);
            }
            printf(
                   "While processing %zd known objects with\n"
                   "     gray_for_allocation %04x\n"
                   "    black_for_allocation %04x\n"
                   "       mask_for_deleting %04x\n"
                   "       mask_for_clearing %04x\n"
                   "      debug_assert_white %04x\n"
                   ,
                   _heap_objects,
                   _gray_for_allocation,
                   _black_for_allocation,
                   _is_sweeping.raw,
                   _is_clearing.raw,
                   _debug_assert_white);

            __builtin_trap();
#endif // !NDEBUG
        }

        // Trace: promote and trace everything the reports delivered --
        // shadelist arrivals, the root registry's standing roots, the weak
        // registry when deciding -- then drain the graystack to fixpoint.
        // (Allocations were promoted at receive; warm-up cohorts at the
        // GRAY -> BLACK transition.)  Cost is proportional to the new work,
        // never to the heap: stage 5 deleted the full pass, and with it the
        // per-object count check -- the sweep's count == 0 delete assert is
        // the standing S1 oracle in its place.
        void collector_trace() {

            ++_scan_passes;
            auto t0 = std::chrono::steady_clock::now();

            assert(global_children.debug_is_empty());

            // validate state:
            assert((_is_sweeping & _is_clearing).raw == 0);
            assert((_is_clearing.raw & _gray_for_allocation) == 0);
            assert((_is_clearing.raw & _black_for_allocation) == 0);

            int counter = 0;

            // Shadelist arrivals: the objects mutators flipped white->gray.
            // The mutator already wrote the gray bits; promotion only
            // blackens where a collection may blacken (warm-up bits wait
            // for the transition walk).  An entry may predate its object's
            // allocation report; the header is dereferenceable through the
            // shader's release/acquire report edge, and no sweep can free
            // an object whose entry is still in flight (any shade of it
            // precedes its unreachability, which precedes the quiet gate by
            // at least the +2 window).
            {
                const GarbageCollected* object = nullptr;
                while (_shaded_arrivals.try_pop(object))
                    _promote(object);
            }

            // Root registry: the standing roots.  In-cycle 0->1 transitions
            // shade (resetting the quiet window); the registry answers the
            // one question transitions cannot: what was already rooted when
            // a cycle began.  Entries observed with count zero are dropped
            // -- the preceding 1->0 shaded, and any re-root files a fresh
            // event.
            //
            // Live entries are grayed only for bits this walk can ALSO
            // blacken.  Graying a warm-up bit here would be legal (4.1's
            // optional early shade) but is a trap: the walk cannot blacken
            // it, does not park it for deferral, and if the entry leaves
            // the registry before the bit blackens, the exit shade's
            // record-once fetch_or finds the bit already gray and files
            // nothing -- orphaning the object gray-not-black (seen as a
            // sweep-time GRAY violation on a dropped World).  Leaving
            // warm-up bits untouched costs nothing: the snapshot point is
            // black-publish, where this walk grays-and-blackens the entry,
            // and an early exit routes through the ordinary shade channel.
            {
                Bag<const GarbageCollected*> keep;
                const GarbageCollected* object = nullptr;
                while (_root_registry.try_pop(object)) {
                    assert(object);
                    int32_t reference_count = object->_count.load_relaxed();
                    if (reference_count == 0)
                        continue;
                    uint16_t before_gray = object->_gray.load_relaxed();
                    uint16_t before_black = object->_black;
                    violation(object, before_gray, before_black, reference_count);
                    uint16_t after_gray;
                    for (;;) {
                        after_gray = (before_gray | _black_for_allocation) & ~_is_clearing.raw;
                        if (after_gray == before_gray)
                            break;
                        if (object->_gray.compare_exchange_weak_relaxed_relaxed(before_gray,
                                                                                after_gray))
                            break;
                    }
                    uint16_t mark_black = after_gray & _black_for_allocation;
                    uint16_t after_black = (before_black | mark_black) & ~_is_clearing.raw;
                    object->_black = after_black;
                    violation(object, after_gray, after_black, reference_count);
                    uint16_t did_set_black = ~before_black & after_black;
                    if (did_set_black) {
                        ++_marked_since_line;
                        _graystack.push(object);
                    }
                    keep.push(object);
                }
                _root_registry.splice(std::move(keep));
            }

            // Depth-first trace to fixpoint.
            {
                const GarbageCollected* parent = nullptr;
                while (_graystack.try_pop(parent)) {
                    assert(parent);
                    uint16_t parent_black = parent->_black & _black_for_allocation;
                    parent->_garbage_collected_scan();
                    const GarbageCollected* child = nullptr;
                    while (global_children.try_pop(child)) {
                        uint16_t before_gray = child->_gray.load_relaxed();
                        uint16_t before_black = child->_black;
                        int32_t reference_count = child->_count.load_relaxed();
                        violation(child, before_gray, before_black, reference_count);
                        uint16_t after_gray;
                        for (;;) {
                            after_gray = (before_gray | parent_black) & ~_is_clearing.raw;
                            if (after_gray == before_gray)
                                break;
                            if (child->_gray.compare_exchange_weak_relaxed_relaxed(before_gray,
                                                                                   after_gray))
                                break;
                        }
                        uint16_t mark_black = after_gray & _black_for_allocation;
                        uint16_t after_black = (before_black | mark_black) & ~_is_clearing.raw;
                        child->_black = after_black;
                        violation(child, after_gray, after_black, reference_count);
                        uint16_t did_set_black = ~before_black & after_black;
                        if (did_set_black) {
                            ++_marked_since_line;
                            _graystack.push(child);
                        }
                    }
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
                }
            }

            // Weak registry: only actual weak holders are visited.  Runs
            // after the drain so the doomed test reads settled gray words;
            // an entry is dropped exactly when this iteration's sweep walk
            // is about to delete it (the mirror of the sweep's any-bit
            // predicate: white for some sweeping bit, and unrooted), so
            // the registry never dangles.
            if ((_is_weak_deciding | _is_sweeping).raw) {
                Bag<const GarbageCollected*> keep;
                const GarbageCollected* object = nullptr;
                while (_weak_registry.try_pop(object)) {
                    assert(object);
                    if (_is_weak_deciding.raw)
                        object->_garbage_collected_decide_weak(_is_weak_deciding.raw,
                                                               _gray_for_allocation,
                                                               _black_for_allocation);
                    uint16_t gray = object->_gray.load_relaxed();
                    int32_t reference_count = object->_count.load_relaxed();
                    bool doomed = (~gray & _is_sweeping.raw)
                        && (reference_count == 0);
                    if (!doomed)
                        keep.push(object);
                }
                _weak_registry.splice(std::move(keep));
            }

            assert(_graystack.debug_is_empty());
            assert(global_children.debug_is_empty());

            // Quiet accounting: this trace ran the graystack dry (reports
            // are received only between traces), so it counts toward every
            // bit's quiet window; and the weak walk ran for every deciding
            // bit.
            for (auto& n : _passes_since_k_work)
                ++n;
            for (int k = 0; k != 16; ++k)
                if (_is_weak_deciding[k])
                    kstate[k].scans += 1;

            if (_marked_since_line | _allocated_since_scan | _shaded_since_scan) {
                auto t1 = std::chrono::steady_clock::now();
                printf("C0: trace marked=%zd,alloc+=%zd,shaded+=%zd,roots=%zd,weak=%zd,heap=%zd in %.3gs\n",
                       std::exchange(_marked_since_line, size_t{0}),
                       std::exchange(_allocated_since_scan, size_t{0}),
                       std::exchange(_shaded_since_scan, size_t{0}),
                       _root_registry.debug_size(),
                       _weak_registry.debug_size(),
                       _heap_objects,
                       std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
            }

        } // void Collector::collector_trace()

        // Sweep: the one heap walk, over only the cohorts old enough to
        // hold a white object for some sweeping bit.  Deletes whites,
        // strips retired (CLEARING) bits from survivors as it goes --
        // clearing rides sweep and costs no walk of its own -- and folds
        // the visited cohorts into the single mature cohort, whose birth
        // order is older than every future gate and therefore spent.
        void collector_sweep_walk() {

            assert(_is_sweeping.raw);
            assert(_graystack.debug_is_empty());

            auto t0 = std::chrono::steady_clock::now();
            uint16_t sweep_mask = _is_sweeping.raw;

            // Newest gate among the sweeping bits: cohorts older than it
            // are eligible for at least one of them.  (A cohort eligible
            // for one sweeping bit but not another is walked with the full
            // mask; that is safe because members born after a bit's gate
            // are marked for it at birth, so the any-bit delete predicate
            // below cannot misfire on them.)
            Epoch newest_gate{};
            bool have_gate = false;
            for (int k = 0; k != 16; ++k) {
                if (_is_sweeping[k]) {
                    newest_gate = have_gate ? std::max(newest_gate, _k_sweep_gate[k])
                                            : _k_sweep_gate[k];
                    have_gate = true;
                }
            }
            assert(have_gate);

            std::deque<Cohort> keep;
            Cohort folded{Epoch{}, true, 0, {}};
            size_t visited = 0;
            size_t delete_count = 0;
            uint16_t stripped = 0;
            int counter = 0;

            for (auto& c : _cohorts) {
                if (!c.mature && !(c.min_epoch < newest_gate)) {
                    // Every member born k-marked for every sweeping k; and
                    // young cohorts cannot carry needs_strip (flags are set
                    // only on cohorts older than the flagging bit's
                    // white-ack, which predates any later sweep gate).
                    keep.push_back(std::move(c));
                    continue;
                }
                uint16_t strip = c.needs_strip;
                stripped |= strip;
                const GarbageCollected* object = nullptr;
                while (c.objects.try_pop(object)) {
                    assert(object);
                    ++visited;
                    uint16_t before_gray = object->_gray.load_relaxed();
                    uint16_t before_black = object->_black;
                    int32_t reference_count = object->_count.load_relaxed();
                    violation(object, before_gray, before_black, reference_count);
                    if (~before_gray & sweep_mask) {
                        // White for ANY sweeping bit: that bit is past its
                        // quiet gate, so its whiteness alone proves the
                        // object was unreachable at that bit's snapshot --
                        // permanently.  Blackness for a concurrently
                        // sweeping bit only records reachability at an
                        // older snapshot and cannot resurrect.  (This also
                        // makes surviving a walk certify the object marked
                        // for every bit the walk swept -- the uniform
                        // certificate 4.11 builds on.)  Rooting requires a
                        // reachable pointer, so a white object cannot be
                        // rooted -- the standing S1 oracle.
                        assert(reference_count == 0);
                        delete object;
                        ++delete_count;
                        --_heap_objects;
                    } else {
                        if (strip) {
                            // The stripped bits are all in CLEARING, which
                            // no mutator can shade; the CAS contends only
                            // with concurrent shades of OTHER bits.
                            uint16_t after_gray;
                            for (;;) {
                                after_gray = before_gray & ~strip;
                                if (after_gray == before_gray)
                                    break;
                                if (object->_gray.compare_exchange_weak_relaxed_relaxed(before_gray,
                                                                                        after_gray))
                                    break;
                            }
                            object->_black = before_black & ~strip;
                        }
                        folded.objects.push(object);
                    }
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
                }
            }
            _cohorts = std::move(keep);
            if (!folded.objects.debug_is_empty())
                _cohorts.push_front(std::move(folded));

            // One walk serves every currently-sweeping bit.
            for (int k = 0; k != 16; ++k)
                if (_is_sweeping[k])
                    kstate[k].scans += 1;

            auto t1 = std::chrono::steady_clock::now();
            printf("C0: sweep mask=%04x visited=%zd deleted=%zd stripped=%04x heap=%zd cohorts=%zd in %.3gs\n",
                   sweep_mask,
                   visited,
                   delete_count,
                   stripped,
                   _heap_objects,
                   _cohorts.size(),
                   std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);

        } // void Collector::collector_sweep_walk()

    }; // struct Collector

    static Collector collector = {};

    void collector_run_on_this_thread() {
        pthread_setname_np("C0");
        collector.loop_until_canceled();
    }

    void collector_cancel() {
        collector._is_canceled.store_relaxed(true);
        // Poke the epoch
        // - wakes the collector, or
        // - proves that another thread is pinned and will wake the collector
        mutator_pin();
        mutator_unpin();
    }

    void collector_register_cycle_callback(uint64_t k,
                                            void* callback) noexcept {
        if (k == 0) {
            global_work_queue_schedule(callback);
        } else {
            std::scoped_lock guard{collector._cycle_waiters_mutex};
            collector._cycle_waiters.emplace_back(k, callback, 0);
            collector._cycle_waiters_count.store_relaxed(collector._cycle_waiters.size());  // TEMP
        }
    }

    // Tripwire for the stage-5 root-walk warm-up orphan: an object rooted
    // across a report boundary (so the root registry holds it) and dropped
    // while some bit is still in gray warm-up must not end up gray-not-black
    // -- the registry walk must not gray bits it cannot blacken, or the exit
    // shade's record-once fetch_or files nothing and the orphan trips the
    // sweep's GRAY violation.  This mirrors the per-frame World swap that
    // exposed the bug in the GUI (root, hold across quiescences, drop),
    // sampled across enough cycles to land in every phase window.
    define_test("gc_root_churn") {
        for (int i = 0; i != 400; ++i) {
            Root<HeapInt64*> r{new HeapInt64(i)};
            co_await Coroutine::SuspendAndSchedule{};
            // r drops here: a 1 -> 0 shade on a registry-resident object.
        }
        // Let several full cycles complete so the sweeps run under the
        // violation checks.
        co_await Coroutine::WaitForCollectionCycles{4};
        co_return;
    };




} // namespace wry


