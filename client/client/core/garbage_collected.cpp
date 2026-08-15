//
//  garbage_collected.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <bit>
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

// ==== WRY_GC_DEBUG instrumentation (2026-08-15) ============================
// Two tiers, born from two rare crashes:
//
// STANDING (keep): _debug_note_unpinned, the dynamic check of the real GC
// entry contract (pinned-ness) at allocate/shade/root_up/register_weak.  It
// found both feeders of the thread-reap lost-report crash; a clean suite is
// silent.  Candidate for print->assert promotion once a GUI session has
// been observed clean.
//
// CRASH-1 TRAP (remove when that question closes): the freed-object ring,
// walk phase/object labels, ASan report narration, and WRY_GC_QUARANTINE
// mode -- forensics for the unresolved collector-trace use-after-free (a
// swept FrozenSkiplistSet Head read by a later child-marking pass; possibly
// a stale incremental build, never reproduced from a clean build).  If it
// stays silent through the save/load milestone under routine
// WRY_GC_QUARANTINE=1 runs, strip this tier in its own commit, stall-
// instrumentation style: recover from git if it ever recurs.
#ifndef NDEBUG
#define WRY_GC_DEBUG 1
#endif
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#include <malloc/malloc.h>
#define WRY_GC_DEBUG_ASAN 1
#endif
#endif
#include <pthread.h>
// ===========================================================================

namespace wry::bump {
    
    // TODO: Something weird happened with inline thread_local that forces this
    // back to ye olde extern model
    thread_local State this_thread_state{};
    
}

namespace wry {
    
    // TODO: Combine with is_pinned?
    enum ThreadMode : uint8_t {
        NONE = 0,
        MUTATOR = 1,
        COLLECTOR = 2,
    };
    
    constinit thread_local ThreadMode _this_thread_mode;

    void assert_this_thread_is_mutator_or_collector() {
        assert((_this_thread_mode == ThreadMode::MUTATOR)
               || (_this_thread_mode == ThreadMode::COLLECTOR));
    }

    void assert_this_thread_is_mutator() {
        assert(_this_thread_mode == ThreadMode::MUTATOR);
    }
    
    void assert_this_thread_is_collector() {
        assert(_this_thread_mode == ThreadMode::COLLECTOR);
    }

    void this_thread_set_is_mutator() {
        assert(_this_thread_mode == NONE);
        _this_thread_mode = ThreadMode::MUTATOR;
    }

    void this_thread_set_is_collector() {
        assert(_this_thread_mode == NONE);
        _this_thread_mode = ThreadMode::COLLECTOR;
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

    // Policy: GC-adjacent TLS must be trivially destructible -- TLS
    // destructor order is the reverse of an invisible first-use order, and
    // destructors run unpinned on dying threads (see the thread-reap
    // lost-report incident, 2026-08).  The four report Bags above are a
    // sanctioned exception: their destructors do no work, only assert
    // emptiness, serving as lost-report tripwires at thread death (as does
    // bump::State's).  Everything else must pass this gate.
    static_assert(std::is_trivially_destructible_v<
                      decltype(_thread_local_gray_for_allocation)>);
    static_assert(std::is_trivially_destructible_v<
                      decltype(_thread_local_black_for_allocation)>);
    static_assert(std::is_trivially_destructible_v<
                      decltype(_thread_local_gray_did_shade)>);
    static_assert(std::is_trivially_destructible_v<
                      decltype(_thread_local_gc_allocated_bytes)>);
    static_assert(std::is_trivially_destructible_v<
                      decltype(_thread_local_gc_allocated_objects)>);

#if WRY_GC_DEBUG
    // ==== TEMP: collector-side crash forensics ====
    // Ring of recently deleted objects (collector thread only).
    struct DebugFreedRecord {
        const void* ptr;
        uint16_t gray;
        uint16_t black;
        uint16_t sweep_mask;
        uint64_t pass;
    };
    constexpr size_t DEBUG_FREED_RING_SIZE = (size_t)1 << 20;
    static DebugFreedRecord* _debug_freed_ring = nullptr;
    static size_t _debug_freed_count = 0;
    // Collector-loop pass counter and what the collector is doing right now.
    static uint64_t _debug_gc_pass = 0;
    static const char* _debug_walk_phase = "(idle)";
    static const GarbageCollected* _debug_walk_object = nullptr;
    static int _debug_window_base = 0;
    static int _debug_next_start = 0;
    static int _debug_live_count = 0;

    // Soft detector for GC traffic from unpinned threads (the lost-report
    // feeder for the thread-reap crash).  Prints the first few offenders
    // per site instead of asserting, so one run maps every bad site.
    static void _debug_note_unpinned(const char* site, const void* ptr) {
        if (epoch::local_state.is_pinned)
            return;
        static Atomic<int64_t> _count{0};
        int64_t n = _count.fetch_add_relaxed(1);
        if (n < 16) {
            char name[64] = {};
            pthread_getname_np(pthread_self(), name, sizeof name);
            fprintf(stderr,
                    "WRY-GC-UNPINNED: %s(%p) on unpinned thread \"%s\" (%p)\n",
                    site, ptr, name, (void*)pthread_self());
        }
    }
#else
    static void _debug_note_unpinned(const char*, const void*) {}
#endif
    // ==== END TEMP ====

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
        // assert_this_thread_is_mutator_or_collector();

        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.

        // TODO: We can require non-null
        _debug_note_unpinned("GarbageCollected()", this); // TEMP
        _thread_local_new_objects.push(this);
    }

    void garbage_collected_shade(GarbageCollected const* ptr) {
        if (ptr) {
            _debug_note_unpinned("shade", ptr); // TEMP
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
        _debug_note_unpinned("root_up", ptr); // TEMP
        // garbage_collected_shade(ptr);
        _thread_local_rooted_objects.push(ptr);
    }

    void garbage_collected_register_weak(GarbageCollected const* ptr) {
        assert(ptr);
        _debug_note_unpinned("register_weak", ptr); // TEMP
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
        // The period's allocation color: colors are loaded at pin/repin,
        // so one quiescence period has one color, and every member of
        // `allocations` was stamped with exactly this gray word at birth.
        // The receive routes the whole bag into a keyed cohort with it
        // (4.11).
        uint16_t gray_for_allocation = 0;
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
            .gray_for_allocation = _thread_local_gray_for_allocation,
            .allocations = std::move(_thread_local_new_objects),
            .shaded = std::move(_thread_local_shaded_objects),
            .rooted = std::move(_thread_local_rooted_objects),
            .weak_registrations = std::move(_thread_local_weak_registrations),
            .epoch = epoch::local_state.known
        };
        // Publish with release so the collector's acquire-exchange reads the
        // report -- and the _gray words of the objects shaded before it --
        // immediately, with no epoch embargo.  Standard push shape:
        // desired->next is written only before each attempt, never after
        // the successful (publishing) exchange.
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

#if WRY_GC_DEBUG && WRY_GC_DEBUG_ASAN
    // ==== TEMP: narrate the collector's state into the ASan report ====
    // Parses the faulting address out of the report text, looks it up in
    // the freed-object ring, and prints what the collector was doing.
    static void _wry_gc_asan_report_callback(const char* text) {
        fprintf(stderr, "WRY-GC-REPORT: pass=%llu phase=%s window=[%d,%d) live=%d\n",
                (unsigned long long)_debug_gc_pass,
                _debug_walk_phase,
                _debug_window_base, _debug_next_start, _debug_live_count);
        for (int k = 0; k != 16; ++k) {
            if (kstate[k].kphase != UNUSED)
                fprintf(stderr, "WRY-GC-REPORT: k=%d %s scans=%d\n",
                        k, _KPhase_names[kstate[k].kphase], kstate[k].scans);
        }
        if (const GarbageCollected* p = _debug_walk_object) {
            if (__asan_region_is_poisoned((void*)p, sizeof(GarbageCollected))) {
                fprintf(stderr,
                        "WRY-GC-REPORT: walk object %p IS ITSELF FREED/POISONED\n",
                        (const void*)p);
            } else {
                fprintf(stderr,
                        "WRY-GC-REPORT: walk object %p count=%d gray=%04x black=%04x -- ",
                        (const void*)p,
                        (int)p->_count.load_relaxed(),
                        (unsigned)p->_gray.load_relaxed(),
                        (unsigned)p->_black);
                p->_garbage_collected_debug();
            }
        }
        // Faulting address, if the report names one.
        const char* s = text ? strstr(text, "on address 0x") : nullptr;
        if (s) {
            uintptr_t addr = (uintptr_t)strtoull(s + 13, nullptr, 16);
            fprintf(stderr, "WRY-GC-REPORT: faulting address %p\n", (void*)addr);
            if (_debug_freed_ring) {
                size_t n = _debug_freed_count < DEBUG_FREED_RING_SIZE
                         ? _debug_freed_count : DEBUG_FREED_RING_SIZE;
                for (size_t back = 1; back <= n; ++back) {
                    const DebugFreedRecord& r =
                        _debug_freed_ring[(_debug_freed_count - back)
                                          & (DEBUG_FREED_RING_SIZE - 1)];
                    uintptr_t base = (uintptr_t)r.ptr;
                    if (addr - base < 4096) {
                        fprintf(stderr,
                                "WRY-GC-REPORT: freed %zu deletes ago: ptr=%p "
                                "gray=%04x black=%04x sweep_mask=%04x pass=%llu "
                                "(%lld passes before now)\n",
                                back, r.ptr,
                                (unsigned)r.gray, (unsigned)r.black,
                                (unsigned)r.sweep_mask,
                                (unsigned long long)r.pass,
                                (long long)(_debug_gc_pass - r.pass));
                        break;
                    }
                }
            }
        }
    }
    // ==== END TEMP ====
#endif

    struct Collector {

        // Keyed cohorts (4.11, cohorts-as-certificates): the known heap,
        // segregated by certificate.  An object's key is the oldest live
        // sweep-pending bit it is not known to be nonwhite for -- the
        // first bit whose sweep might yet delete it.  k's sweep therefore
        // visits exactly cohort k; every other cohort's key is a
        // certificate that k's visit could not matter.  Objects fully
        // marked for every live bit key to the future slot (_next_start),
        // which the next collection adopts as its own cohort when it
        // starts -- everything accumulated there is white for the new bit
        // by construction.
        //
        // Keys come from two places, one computation (_route_for_gray):
        // newborn reports route by their period's allocation color
        // (strictly more precise than the retired birth-epoch-versus-
        // sweep-gate arithmetic), and sweep survivors reroute by their
        // observed color word (marks are set-only until a bit's own
        // strip, so an observed mark is stable through that bit's sweep).
        // The routing is a rotate-and-ffs because bits start and retire
        // in strict cyclic position order, keeping the live window
        // contiguous.
        //
        // needs_strip carries clearing exactly as before, per keyed
        // cohort: when a bit enters CLEARING it is flagged on every
        // nonempty cohort; a sweep visit strips the flagged bits from
        // survivors before rerouting them (so routing cannot spread stale
        // marks); late pre-ack reports carry the stale bit in their
        // allocation color and flag their target cohort at receive; the
        // bit recycles when no cohort is flagged.
        struct Cohort {
            uint16_t needs_strip;
            Bag<const GarbageCollected*> objects;
        };
        std::array<Cohort, 16> _cohorts_by_key = {};
        size_t _heap_objects = 0;

        // The live window, kept cyclically contiguous by the strict
        // start/retire order: _window_base is the oldest live bit's
        // position (== _next_start when none are live); _next_start is
        // the position the next collection will take, and until it does,
        // the key meaning "not white for any live bit".  _live_count is
        // capped (see the UNUSED transition) so a start is always
        // possible: a full window would deadlock retirement against the
        // never-started future cohort's strip flags.
        int _window_base = 0;
        int _next_start = 0;
        int _live_count = 0;

        // Bits whose sweep is still ahead: being white for one of these
        // is the only way an object can still be deleted.
        uint16_t _sweep_pending_mask() const {
            return (_is_gray_published | _is_black_published |
                    _is_weak_deciding | _is_sweeping).raw;
        }

        // The keyed-cohort routing (4.11): the oldest candidate bit, in
        // window order, that `gray` is white for; the future slot if
        // none.  candidates must be a subset of the live window.
        int _route_for_gray(uint16_t gray, uint16_t candidates) const {
            uint16_t whites = (uint16_t)(~gray & candidates);
            if (!whites)
                return _next_start;
            return (std::countr_zero((uint16_t)std::rotr(whites, _window_base))
                    + _window_base) & 15;
        }

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
        // time any kbit transitions CLEARING -> UNUSED (i.e., one full cycle
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
            }
            for (auto& w : ready)
                global_work_queue_schedule(w.callback);
        }

        ~Collector() {
            for (auto& c : _cohorts_by_key)
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
            uint16_t pending = _sweep_pending_mask();
            while (head) {
                Epoch H = Epoch{head->epoch};

                // H is the publisher's pinned epoch; concurrently pinned
                // threads span at most one epoch ahead of us.  (No lower
                // bound: reports may have queued across several of our
                // pass-lengthened iterations.)
                assert(H <= E + 1);

                if (!head->allocations.is_empty()) {
                    size_t n = head->allocations.size();
                    _allocated_since_scan += n;
                    _heap_objects += n;
                    // Route the whole bag by the period's allocation
                    // color: one color per quiescence period, so every
                    // member was born with exactly these gray bits, and
                    // the key -- the oldest sweep-pending bit a member
                    // might be white for -- is shared (4.11; strictly
                    // more precise than birth-epoch arithmetic against
                    // sweep gates).
                    int key = _route_for_gray(head->gray_for_allocation, pending);
                    // A sweeping bit is past its quiet gate: no report
                    // can still carry allocations white for it (the
                    // completeness lemma), so newborns never route into a
                    // cohort this iteration's walk will sweep.
                    assert(!((uint16_t)(1u << key) & _is_sweeping.raw));
                    Cohort& c = _cohorts_by_key[key];
                    // Late-report stripping: a mutator that loaded its
                    // colors before k's white publish delivers k-marked
                    // allocations after the CLEARING transition's
                    // flagging pass ran; the stale mark is right there in
                    // the allocation color, so flag the target directly.
                    // (Sound against the recycle check by ordering: a
                    // pre-ack pin blocks the epoch, so the straggler's
                    // report is received -- and its cohort flagged --
                    // before the try_advance that could retire k, receive
                    // running first in the iteration.)
                    c.needs_strip |= head->gray_for_allocation & _is_clearing.raw;
                    // Receive-time promotion: gray-born objects blacken
                    // here once their bit may blacken; those born for a
                    // bit still warming up park in _deferred_warmup (via
                    // _promote) for that bit's GRAY -> BLACK transition.
                    // Everything born after a black-ack is black at birth
                    // and no-ops.
                    for (const GarbageCollected* object : head->allocations)
                        _promote(object);
                    c.objects.splice(std::move(head->allocations));
                }
                _shaded_since_scan += head->shaded.size();
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
            assert_this_thread_is_collector();

            mutator_pin();
            thread_public_register("C0");
            assert(epoch::local_state.is_pinned);
            epoch::Epoch epoch_at_last_change = epoch::local_state.known;

            printf("C0: garbage collector starts\n");

#if WRY_GC_DEBUG
            // TEMP: forensics ring + ASan report narration
            if (!_debug_freed_ring)
                _debug_freed_ring = (DebugFreedRecord*)calloc(DEBUG_FREED_RING_SIZE,
                                                              sizeof(DebugFreedRecord));
#if WRY_GC_DEBUG_ASAN
            __asan_set_error_report_callback(&_wry_gc_asan_report_callback);
#endif
#endif

            while (!_is_canceled.load_relaxed()) {

#if WRY_GC_DEBUG
                ++_debug_gc_pass;                    // TEMP
                _debug_window_base = _window_base;   // TEMP
                _debug_next_start = _next_start;     // TEMP
                _debug_live_count = _live_count;     // TEMP
                _debug_walk_phase = "receive";       // TEMP
                _debug_walk_object = nullptr;        // TEMP
#endif

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
            // *Has all the work been done?* -- i.e., has every known object been
            // visited? Answered by `kstate[k].scans >= 1`. Used by `SWEEPING`,
            // `CLEARING`. Safe because objects we haven't yet seen are
            // guaranteed to be in the target state by the previous phase's
            // invariant.
            //
            // *What did the mutators actually do?* -- i.e., has k-work stopped
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
                        // Strict cyclic start order (4.11): only the next
                        // slot in rotation may start, so the live window
                        // stays contiguous and keyed-cohort routing is a
                        // rotate-and-ffs.
                        if (!first)
                            break;
                        if (k != _next_start)
                            break;
                        // The slot after must be free to become the future
                        // cohort, and the window must not fill: retirement
                        // is in-order and waits on strip flags that only a
                        // future start can discharge, so a full window
                        // deadlocks.  (An admission cap of ~2, the next
                        // performance lever, would subsume this bound.)
                        if (!_is_unused[(k + 1) & 15])
                            break;
                        if (_live_count >= 12)
                            break;
                        first = false;
                        // The future slot we vacate holds exactly this
                        // bit's population -- everything accumulated there
                        // is k-white by construction.  The slot we adopt
                        // as the new future was drained by its own final
                        // sweep and never routed into since.
                        assert(_cohorts_by_key[(k + 1) & 15].objects.is_empty());
                        if (_live_count == 0)
                            _window_base = k;
                        ++_live_count;
                        _next_start = (k + 1) & 15;
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
                        // (No sweep gate to record: newborn cohorts key by
                        // their reports' allocation color, which carries
                        // "born k-marked" exactly rather than bounding it
                        // with epoch arithmetic.)
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
                        // Flag k for stripping on every nonempty keyed
                        // cohort: at the white publish every live object
                        // was k-marked, wherever its key placed it.
                        // Cohorts that gain members later stay clean --
                        // late pre-ack reports carry k in their allocation
                        // color and flag their target at receive, and
                        // sweep-rerouted survivors move stripped.
                        for (auto& c : _cohorts_by_key)
                            if (!c.objects.is_empty())
                                c.needs_strip |= bit;
                    } break;

                    case CLEARING: {
                        // Retire strictly in window order (4.11): the
                        // rotate-and-ffs keying needs the live window
                        // contiguous, so a bit waits for its elders even
                        // when its own strips are done.
                        if (k != _window_base)
                            break;
                        // Clearing rides sweep: k has been stripped from a
                        // cohort's members when the flag is gone.  k
                        // recycles when no cohort carries it.  Recycling
                        // can now wait on cohorts that only their own
                        // key's sweep visits -- the accepted latency of
                        // certificate skipping; the admission bound keeps
                        // a future start (hence that sweep) always
                        // reachable.
                        bool pending = false;
                        for (auto& c : _cohorts_by_key)
                            if (c.needs_strip & bit) {
                                pending = true;
                                break;
                            }
                        if (pending)
                            break;
                        kstate[k] = { UNUSED, E, 0 };
                        --_live_count;
                        _window_base = _live_count ? (k + 1) & 15 : _next_start;
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
#if WRY_GC_DEBUG
                    _debug_walk_phase = "root-registry"; // TEMP
                    _debug_walk_object = object;         // TEMP
#endif
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
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
                }
                _root_registry.splice(std::move(keep));
            }

            // Depth-first trace to fixpoint.
            {
                const GarbageCollected* parent = nullptr;
                while (_graystack.try_pop(parent)) {
                    assert(parent);
#if WRY_GC_DEBUG
                    _debug_walk_phase = "trace-children"; // TEMP: the child
                    _debug_walk_object = parent;          // reads below run
#endif                                                    // under this parent
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
#if WRY_GC_DEBUG
                    _debug_walk_phase = "weak-registry"; // TEMP
                    _debug_walk_object = object;         // TEMP
#endif
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
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
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
                       _root_registry.size(),
                       _weak_registry.size(),
                       _heap_objects,
                       std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
            }

        } // void Collector::collector_trace()

        // Sweep: visits exactly the sweeping bits' own cohorts (4.11,
        // cohorts-as-certificates).  Every object possibly white for k
        // keys at or before k, sweeps run in window order, and older keys
        // were emptied by older sweeps, so cohort k is precisely k's
        // candidate set; every other cohort's key certifies the visit
        // could not delete, and it is skipped.  Deletes whites, strips
        // retired (CLEARING) bits from survivors as it goes -- clearing
        // rides sweep and costs no walk of its own -- and reroutes each
        // survivor to the cohort of the oldest still-pending bit its
        // observed word is white for, the future slot if none.
        void collector_sweep_walk() {

            assert(_is_sweeping.raw);
            assert(_graystack.debug_is_empty());

            auto t0 = std::chrono::steady_clock::now();
            uint16_t sweep_mask = _is_sweeping.raw;

            // Reroute candidates: live sweep-pending bits whose fate this
            // walk does not decide.  A survivor's new key is the oldest
            // of these its observed word is white for -- observed marks
            // are set-only until the marked bit's own strip, so they are
            // stable through that bit's sweep, and routing past it is a
            // certificate against its walk.  Fully marked survivors key
            // to the future slot.
            uint16_t candidates = (uint16_t)(_sweep_pending_mask() & ~sweep_mask);

            size_t visited = 0;
            size_t delete_count = 0;
            uint16_t stripped = 0;
            int counter = 0;

            for (int key = 0; key != 16; ++key) {
                uint16_t key_bit = (uint16_t)(1u << key);
                if (!(key_bit & sweep_mask))
                    // Certificate: every member of this cohort is nonwhite
                    // for every sweep-pending bit older than its key,
                    // which includes every sweeping bit -- the visit could
                    // not delete, so it is skipped.
                    continue;
                Cohort& c = _cohorts_by_key[key];
                uint16_t strip = std::exchange(c.needs_strip, 0);
                stripped |= strip;
                // Key-invariant oracle: members promised nonwhite for
                // every sweep-pending bit older than their key in window
                // order.
                [[maybe_unused]] uint16_t older_than_key =
                    (uint16_t)std::rotl((uint16_t)((1u << ((key - _window_base) & 15)) - 1u),
                                        _window_base);
                const GarbageCollected* object = nullptr;
                while (c.objects.try_pop(object)) {
                    assert(object);
#if WRY_GC_DEBUG
                    _debug_walk_phase = "sweep"; // TEMP
                    _debug_walk_object = object; // TEMP
#endif
                    ++visited;
                    uint16_t before_gray = object->_gray.load_relaxed();
                    uint16_t before_black = object->_black;
                    int32_t reference_count = object->_count.load_relaxed();
                    violation(object, before_gray, before_black, reference_count);
                    assert(!(~before_gray & older_than_key & (uint16_t)(sweep_mask | candidates)));
                    // Stale clearing marks reach a cohort only through its
                    // flags: transition flagging covers residents, receive
                    // flagging covers late newborns, and rerouted
                    // survivors were stripped before they moved.
                    assert(!(before_gray & _is_clearing.raw & ~strip));
                    if (~before_gray & sweep_mask) {
                        // White for ANY sweeping bit: that bit is past its
                        // quiet gate, so its whiteness alone proves the
                        // object was unreachable at that bit's snapshot --
                        // permanently.  Blackness for a concurrently
                        // sweeping bit only records reachability at an
                        // older snapshot and cannot resurrect.  Rooting
                        // requires a reachable pointer, so a white object
                        // cannot be rooted -- the standing S1 oracle.
                        assert(reference_count == 0);
#if WRY_GC_DEBUG
                        if (_debug_freed_ring) { // TEMP: record the delete
                            DebugFreedRecord& r =
                                _debug_freed_ring[_debug_freed_count++
                                                  & (DEBUG_FREED_RING_SIZE - 1)];
                            r = {object, before_gray, before_black,
                                 sweep_mask, _debug_gc_pass};
                        }
#endif
#if WRY_GC_DEBUG_ASAN
                        // TEMP: WRY_GC_QUARANTINE=1 turns the rare
                        // read-after-sweep flake into a deterministic
                        // use-after-poison report: swept objects are
                        // poisoned and leaked (no destructor) so ANY late
                        // touch -- even one that would have landed in
                        // still-valid recycled memory -- reports at once.
                        static const bool _debug_quarantine =
                            getenv("WRY_GC_QUARANTINE") != nullptr;
                        if (_debug_quarantine) {
                            __asan_poison_memory_region(object,
                                                        malloc_size(object));
                        } else
#endif
                        {
                            delete object;
                        }
                        ++delete_count;
                        --_heap_objects;
                    } else {
                        uint16_t after_gray = before_gray;
                        if (strip) {
                            // The stripped bits are all in CLEARING, which
                            // no mutator can shade; the CAS contends only
                            // with concurrent shades of OTHER bits.
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
                        // Reroute by the observed (post-strip) word; a
                        // concurrent shade we miss only under-certifies,
                        // costing an extra future visit, never a wrong
                        // skip.  Never routes into a swept cohort.
                        int route = _route_for_gray(after_gray, candidates);
                        assert(!((uint16_t)(1u << route) & sweep_mask));
                        _cohorts_by_key[route].objects.push(object);
                    }
                    if (++counter > 1000) {
                        mutator_repin(); counter = 0;
                    }
                }
            }

            // One walk serves every currently-sweeping bit.
            for (int k = 0; k != 16; ++k)
                if (_is_sweeping[k])
                    kstate[k].scans += 1;

            int nonempty = 0;
            for (auto& c : _cohorts_by_key)
                if (!c.objects.is_empty())
                    ++nonempty;
            auto t1 = std::chrono::steady_clock::now();
            printf("C0: sweep mask=%04x visited=%zd deleted=%zd stripped=%04x heap=%zd cohorts=%d window=[%d,%d) in %.3gs\n",
                   sweep_mask,
                   visited,
                   delete_count,
                   stripped,
                   _heap_objects,
                   nonempty,
                   _window_base,
                   _next_start,
                   std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);

        } // void Collector::collector_sweep_walk()

    }; // struct Collector

    static Collector collector = {};

    void collector_run_on_this_thread() {
        this_thread_set_is_collector();
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


