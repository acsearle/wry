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
        
        Bag<const GarbageCollected*> _known_objects;

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
            _known_objects.leak();
            _shaded_arrivals.leak();
            _root_registry.leak();
            _weak_registry.leak();
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
            while (head) {
                Epoch H = Epoch{head->epoch};

                // H is the publisher's pinned epoch; concurrently pinned
                // threads span at most one epoch ahead of us.  (No lower
                // bound: reports may have queued across several of our
                // pass-lengthened iterations.)
                assert(H <= E + 1);

                _allocated_since_scan += head->allocations.debug_size();
                _shaded_since_scan += head->shaded.debug_size();
                _known_objects.splice(std::move(head->allocations));
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

                    for (int k = 0; k != 16; ++k) {
                        kstate[k].scans++;
                    }
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
                // epoch::unpin_this_thread();

                // TODO: Resumable partial scans
                // Once we have enough work for a pass to last O(10ms) we
                // should dip out periodically to open more reports and get more
                // objects
                collector_scans();

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
            // with it.  Leak the record as _known_objects is leaked, or the
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
                        //     warm-up allocation is already in
                        //     _known_objects / _shaded_arrivals;
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
                        // (3) at least one scan completed after the last
                        //     k-work was received, so that work has been
                        //     traced to fixpoint (scans drain the graystack
                        //     before returning), and rooted-but-unshaded
                        //     objects have been grayed by the scan's own
                        //     root check.
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
                    } break;
                        
                    case CLEARING:
                        // Wait for at least one scan to complete
                        if (!kstate[k].scans)
                            break;
                        // All objects are now k-white
                        kstate[k] = { UNUSED, E, 0 };
                        printf("C0: k=%d cycle complete: passes=%llu in %.3gs\n",
                               k,
                               (unsigned long long)(_scan_passes - _cycle_pass0[k]),
                               std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - _cycle_t0[k]).count());
                        _on_cycle_completed(bit);
                        break;
                        
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
                   "While scanning %zd objects with\n"
                   "     gray_for_allocation %04x\n"
                   "    black_for_allocation %04x\n"
                   "       mask_for_deleting %04x\n"
                   "       mask_for_clearing %04x\n"
                   "      debug_assert_white %04x\n"
                   ,
                   _known_objects.debug_size(),
                   _gray_for_allocation,
                   _black_for_allocation,
                   _is_sweeping.raw,
                   _is_clearing.raw,
                   _debug_assert_white);

            __builtin_trap();
#endif // !NDEBUG
        }

        void collector_scans() {

#pragma mark Scan all known objects

            Bag<const GarbageCollected*> survivors;

            size_t trace_count = 0;
            size_t mark_count = 0;
            size_t delete_count = 0;
            size_t scan_count = 0;
            size_t rescue_count = 0;
            auto t0 = std::chrono::steady_clock::now();
            ++_scan_passes;

            assert(_graystack.debug_is_empty());
            assert(survivors.debug_is_empty());
            assert(global_children.debug_is_empty());

            // validate state:

            assert((_is_sweeping & _is_clearing).raw == 0);
            assert((_is_clearing.raw & _gray_for_allocation) == 0);
            assert((_is_clearing.raw & _black_for_allocation) == 0);

#ifndef NDEBUG
            for (GarbageCollected const* object : _known_objects) {
                uint16_t gray  = object->_gray.load_relaxed();
                uint16_t black = object->_black;
                int32_t count = object->_count.load_relaxed();
                violation(object, gray, black, count);
            }
#endif

            int counter = 0;

            // Shadelists: seed the trace wavefront with the objects mutators
            // reported flipping white->gray, instead of waiting for this pass
            // to rediscover them.  The mutator already wrote the gray bits;
            // here we only promote gray to black where a collection is in a
            // blackening phase, and enqueue for tracing -- the same marking
            // step the loop below applies to its own discoveries, minus the
            // root check and sweep.  Bits whose collection is still in gray
            // warm-up are deliberately NOT promoted (mark_black masks them
            // out); the pass will blacken them once k-black publishes.
            //
            // An entry may predate its object's allocation report.  That is
            // safe: the header is dereferenceable (construction
            // happened-before the shader's use of the pointer, which
            // happened-before its report, which is embargo-gated like any
            // other), _black stays collector-owned, and no sweep can free
            // the object before its entry is read -- the weakest case, a
            // shade recorded by a still-j-white mutator of an object that
            // then dies for j, is readable at most 3 epochs after the shade,
            // while j's sweep is at least ~6 epochs from a start at most 2
            // epochs before it.
            {
                const GarbageCollected* object = nullptr;
                while (_shaded_arrivals.try_pop(object)) {
                    assert(object);
                    uint16_t before_gray = object->_gray.load_relaxed();
                    uint16_t before_black = object->_black;
                    int32_t reference_count = object->_count.load_relaxed();
                    violation(object, before_gray, before_black, reference_count);
                    // _black_for_allocation is disjoint from _is_clearing
                    // (a bit is in exactly one phase), so this can neither
                    // set nor resurrect a clearing bit.
                    uint16_t mark_black = before_gray & _black_for_allocation;
                    uint16_t after_black = before_black | mark_black;
                    uint16_t did_set_black = ~before_black & after_black;
                    if (did_set_black) {
                        object->_black = after_black;
                        ++mark_count;
                        _graystack.push(object);
                    }
                }
            }

            // Root registry (stage 4): the standing roots.  In-cycle 0->1
            // transitions shade (and so reset the quiet window); the
            // registry answers the one question transitions cannot: what
            // was already rooted when a cycle began.  Entries observed with
            // count zero are dropped -- the preceding 1->0 shaded, and any
            // re-root files a fresh event.  Live entries are grayed for
            // every active collection and promoted exactly as the pass
            // would; the pass's own per-object count check remains, for
            // now, as the differential oracle (`rescues` below must read
            // zero, modulo in-flight root-up reports, before stage 5 may
            // delete it).
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
                        after_gray = (before_gray | _gray_for_allocation) & ~_is_clearing.raw;
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
                        ++mark_count;
                        _graystack.push(object);
                    }
                    keep.push(object);
                }
                _root_registry.splice(std::move(keep));
            }

            // Weak registry (stage 4): only actual weak holders are visited,
            // replacing the per-object virtual decide in the pass body.  An
            // entry is dropped exactly when this pass's sweep is about to
            // delete it: white for every currently-sweeping bit and
            // unrooted, so the pass body cannot rescue it.  (Arrivals and
            // roots were drained above, so the gray word read here is what
            // the pass body will see.)
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
                    bool doomed = _is_sweeping.raw
                        && !(_is_sweeping.raw & gray)
                        && (reference_count == 0);
                    if (!doomed)
                        keep.push(object);
                }
                _weak_registry.splice(std::move(keep));
            }

            // While any objects are unprocessed
            for (;;) {

                // Depth-first recusively trace all the known children

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
                        // uint16_t did_set_gray  = ~before_gray  & after_gray;
                        uint16_t did_set_black = ~before_black & after_black;
                        if (did_set_black) {
                            ++mark_count;
                            _graystack.push(child);
                        }
                    }
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
                }

                // Resume scanning each object in turn.
                // (Many will have already been processed by tracing)

                const GarbageCollected* object = nullptr;
                if (!_known_objects.try_pop(object))
                    break;
                assert(object);
                ++scan_count;

                // (Weak decisions moved to the registry walk above.)

                // Process root set
                int32_t reference_count = object->_count.load_relaxed();
                uint16_t root_gray = reference_count ? _gray_for_allocation : 0;

                uint16_t before_gray = object->_gray.load_relaxed();
                uint16_t before_black = object->_black;
                uint16_t after_gray;
                for (;;) {
                    violation(object, before_gray, before_black, reference_count);
                    after_gray = (before_gray | root_gray) & ~_is_clearing.raw;
                    if (after_gray == before_gray)
                        break;
                    if (object->_gray.compare_exchange_weak_relaxed_relaxed(before_gray,
                                                                            after_gray))
                        break;
                    // Compare exchange failed, start over
                }
                
                // If gray, and black is active, make it black
                // If in clearing mask, clear it
                
                uint16_t mark_black = after_gray & _black_for_allocation;
                uint16_t after_black = (before_black | mark_black) & ~_is_clearing.raw;
                object->_black = after_black;
                violation(object, after_gray, after_black, reference_count);

                // A rescue is this count-check graying a rooted object the
                // registry machinery had not already grayed -- the
                // differential signal that must sit at zero (modulo
                // in-flight root-up reports) before stage 5 may delete the
                // per-object count check.
                uint16_t did_set_gray = ~before_gray & after_gray;
                if (reference_count && did_set_gray)
                    ++rescue_count;

                uint16_t did_set_black = ~before_black & after_black;

                if (did_set_black) {
                    ++trace_count;
                    _graystack.push(object);
                }

                if (_is_sweeping.raw && !(_is_sweeping.raw & after_gray)) {
                    assert(!did_set_black);
                    // Trace-complete and quiet: nothing reachable is white,
                    // and rooting requires a reachable pointer -- so a white
                    // object here cannot be rooted.  This is the S1 oracle
                    // that registry-driven root seeding must keep true.
                    assert(reference_count == 0);
                    delete object;
                    ++delete_count;
                } else {
                    survivors.push(std::move(object));
                }

                if (++counter > 1000) {
                    mutator_repin(); counter = 0;
                }

            } // loop until no objects
            
            assert(_graystack.debug_is_empty());
            assert(_known_objects.debug_is_empty());
            assert(global_children.debug_is_empty());
            _known_objects.swap(survivors);

            // This scan traced everything received before it started
            // (reports are received only between scans, and the graystack is
            // drained above), so it counts toward every bit's quiet window.
            for (auto& n : _passes_since_k_work)
                ++n;

            auto t1 = std::chrono::steady_clock::now();
            
            printf("C0: scanned=%zd,marked=%zd,deleted=%zd,alloc+=%zd,shaded+=%zd,roots=%zd,weak=%zd,rescues=%zd in %.3gs\n",
                   scan_count,
                   trace_count + mark_count,
                   delete_count,
                   std::exchange(_allocated_since_scan, size_t{0}),
                   std::exchange(_shaded_since_scan, size_t{0}),
                   _root_registry.debug_size(),
                   _weak_registry.debug_size(),
                   rescue_count,
                   std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
            

        } // void Collector::scan()

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




} // namespace wry


