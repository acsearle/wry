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
#include <thread>
#include <queue>
#include <deque>
#include <map>

#include "garbage_collected.hpp"

#include "bag.hpp"
#include "epoch_allocator.hpp"
#include "HeapString.hpp"
#include "stack.hpp"
#include "utility.hpp"
#include "value.hpp"
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
        
    };
    
    
    
    
    

#pragma mark - Forward declarations

    using namespace detail;

#pragma mark - Global and thread_local variables

    constinit thread_local uint16_t _thread_local_gray_for_allocation = 0;
    constinit thread_local uint16_t _thread_local_black_for_allocation = 0;
    constinit thread_local uint16_t _thread_local_gray_did_shade = 0;
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;

    GarbageCollected::GarbageCollected()
    : _gray{_thread_local_gray_for_allocation}
    , _black{_thread_local_black_for_allocation}
    , _count{0}
    , _debug_allocation_gray{_thread_local_gray_for_allocation}
    , _debug_allocation_black{_thread_local_black_for_allocation}
    , _debug_allocation_epoch{epoch::local_state.known.raw}
    {
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.
        _thread_local_new_objects.push(this);
    }

    void GarbageCollected::_garbage_collected_shade() const {
        const uint16_t gray = _thread_local_gray_for_allocation;
        const uint16_t before = _gray.fetch_or_relaxed(gray);
        const uint16_t did_shade = gray & ~before;
        _thread_local_gray_did_shade |= did_shade;
    }

    constinit Stack<GarbageCollected const*> global_children;

    void garbage_collected_scan(GarbageCollected const* child) {
        if (child) {
            global_children.push(child);
        }
    }

    void garbage_collected_scan_weak(GarbageCollected const* child) {
        abort();
    }



    struct Report {

        Report* next = nullptr;
        uint16_t gray_did_shade = 0;
        Bag<const GarbageCollected*> allocations;
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
            .epoch = epoch::local_state.known
        };
        // SAFETY: We perform a RELAXED write.  It's not safe for the collector
        // to dereference this pointer until the epoch has advanced.
        desired->next = _global_atomic_reports_head.load_relaxed();
        while (!_global_atomic_reports_head.compare_exchange_weak_relaxed_relaxed(desired->next,
                                                                                  desired))
            ;
        
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
    }

    void mutator_repin() {
        _mutator_publishes_report();
        // must publish report *before* unpinning
        epoch::repin_this_thread();
        // must load the color state *after* pinning
        _mutator_load_color();
    }

    void mutator_unpin() {
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
        SWEEPING,        // Mutators are black.  Collector deletes white objects.
                         // ...when all objects have been visited...
        WHITE_PUBLISHED, // Mutators are becoming white.  Collector waits.
                         // ...when all mutators are white...
        CLEARING,        // Mutators are white.  Collector clears bits.
                         // ...when all objects have been visited...
        
    };
    
    const char* _KPhase_names[] = { "UNUSED", "GRAY_PUBLISHED", "BLACK_PUBLISHED", "SWEEPING", "WHITE_PUBLISHED", "CLEARING" };
    
    struct KState {
        KPhase kphase;
        Epoch since;
        int scans;
    };
    
    std::array<KState, 16> kstate = {};
    
    
    
    
    struct EmbargoedReport {
        epoch::Epoch received;
        Report* report;
    };
        
    struct ScanRecord {
        epoch::Epoch before;
        epoch::Epoch after;
        uint16_t black;
        epoch::Epoch finalized;
    };
    
        
    struct Collector {
        
        Bag<const GarbageCollected*> _known_objects;

        uint16_t _gray_for_allocation = 0;
        uint16_t _black_for_allocation = 0;
        uint16_t _mask_for_deleting = 0;
        uint16_t _mask_for_clearing = 0;
        
        uint16_t _debug_assert_white;
        uint16_t _debug_assert_nonblack;

        Atomic<bool> _is_canceled;

        Stack<const GarbageCollected*> _graystack;
        
        // Logging structures
        //
        // They are all "locally" monotonic in Epoch.
        //
        // Epoch is cyclic, but only on much timescales much longer than the
        // timescales of the collector.  When we compare Epochs, we account for
        // the representation wrapping, and we trap if the Epochs are too far
        // apart.

        std::deque<EmbargoedReport> _embargoed_reports;
        std::deque<ScanRecord> _scan_history;
        std::array<Epoch, 16> _shade_most_recent;
        
        Epoch _finalized{0xFFFE};
                                
        ~Collector() {
            _known_objects.leak();
        }

        void collector_takes_reports(){
            Report* head =  _global_atomic_reports_head.exchange_relaxed(nullptr);
            assert(epoch::local_state.is_pinned);
            epoch::Epoch E = epoch::local_state.known;
            _embargoed_reports.emplace_back(E, head);
        }
        
        void collector_reads_reports() {
            assert(epoch::local_state.is_pinned);
            Epoch E = epoch::local_state.known;
            auto iterator = _embargoed_reports.begin();
            while (iterator != _embargoed_reports.end()) {
                Epoch F = iterator->received;
                // Reports were received at F
                // Mutators still might be at F-1
                // The last reports we can be sure we have got are from F-2
                Epoch G = F - 2;
                if (E >= F + 3) {
                    Report* head = iterator->report;
                    while (head) {
                        Epoch H = Epoch{head->epoch};

                        // H is the mutator's pinned epoch at publication. F is
                        // the collector's pinned epoch at receipt.  When the
                        // report was received, the collector was pinning either
                        // the current epoch, or the previous epoch, so the
                        // latest the report could have originated is F+1.
                        assert(H <= F + 1);
                        
                        // We claim to have finalized the reports for the epoch
                        // `_finalized` and earlier.  Thus H should come later
                        assert(_finalized < H);
                        
                        _known_objects.splice(std::move(head->allocations));
                        for (int k = 0; k != 16; ++k) {
                            uint16_t bit = 1 << k;
                            if (head->gray_did_shade & bit) {
                                Epoch P = _shade_most_recent[k];
                                _shade_most_recent[k] = std::max(P, H);
                            }
                        }
                        delete std::exchange(head, head->next);
                    }
                    assert(G > _finalized);
                    _finalized = G;
                    iterator = _embargoed_reports.erase(iterator);
                } else {
                    break;
                }
            }
        }

        void loop_until_canceled() {
            
            _this_thread_mode = ThreadMode::COLLECTOR;
            
            epoch::pin_this_thread();
            assert(epoch::local_state.is_pinned);
            epoch::Epoch epoch_at_last_change = epoch::local_state.known;

            printf("C0: garbage collector starts\n");

            while (!_is_canceled.load_relaxed()) {
                
                assert(epoch::local_state.is_pinned);
                epoch::Epoch current_epoch = epoch::local_state.known;

                if (current_epoch != epoch_at_last_change) {
                                                            
                    collector_takes_reports();
                    collector_reads_reports();
                    
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
                
                assert(epoch::local_state.is_pinned);
                Epoch A{epoch::local_state.known};
                epoch::unpin_this_thread();
                
                // TODO: Resumable partial scans
                // Once we have enough work for a pass to last O(10ms) we
                // should dip out periodically to open more reports and get more
                // objects
                collector_scans();
                                
                epoch::pin_this_thread();
                epoch::wait(A);
                assert(epoch::local_state.is_pinned);
                Epoch B{epoch::local_state.known};
                _scan_history.emplace_back(A, B, _black_for_allocation, _finalized);
                
            } // while (!_is_cancelled.load_relaxed())

        } // void Collector::loop_until_canceled()
        


        void try_advance_collection_phases() {
            
            // Each phase transition asks one of three kinds of question:
            //
            // *Has time passed?* - i.e., have all mutators observed a color
            // publish, or have all reports up to some epoch been finalized?
            // Answered by counting epochs against` kstate[k].since` or
            // comparing `_finalized` to a fixed offset. Used by
            // `GRAY_PUBLISHED`, `WHITE_PUBLISHED`.
            //
            // *Has all the work been done?* — i.e., has every known object been
            // visited? Answered by `kstate[k].scans >= 1`. Used by `SWEEPING`,
            // `CLEARING`. Safe because objects we haven't yet seen are
            // guaranteed to be in the target state by the previous phase's
            // invariant.
            //
            // *What did the mutators actually do?* — i.e., did a scan complete
            // with no concurrent k-shading? Answered by `_scan_history` +
            // `_shade_most_recent`. Used only by `BLACK_PUBLISHED`, because
            // tracing termination depends on what the mutators wrote, not just
            // on time.
            
            assert(epoch::local_state.is_pinned);
            epoch::Epoch E = epoch::local_state.known;

            bool first = true;
            
            
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
                        break;
                        
                    case GRAY_PUBLISHED:
                        // Wait until all mutators have updated to run k-gray.
                        // We don't need to wait for reports or scans.
                        if (E < kstate[k].since + 2)
                            break;
                        kstate[k] = { BLACK_PUBLISHED, E, 0 };
                        break;
                        
                    case BLACK_PUBLISHED: {

                        Epoch F = kstate[k].since;

                        // We can move bit k from TRACING to SWEEPING when:
                        //
                        // (1) all k-gray-allocating reports have been spliced
                        //     into _known_objects, and
                        // (2) at least one scan with bit k in the tracing role
                        //     has run after (1), promoting any late arrivals
                        //     to k-black, and
                        // (3) no mutator has shaded a k-white object during
                        //     any post-TRACING epoch whose did_shade is
                        //     finalized.

                        // Condition (1)
                        // We publish BLACK in epoch F
                        // All mutators are BLACK in epoch F + 2
                        // We need to have opened all reports from F + 1, so
                        // that `_known_objects` contains every non-k-black
                        // object.
                        if (_finalized < F + 1)
                            break;
                        
                        for (auto& s : _scan_history) {
                            // (2a) must mark k-black
                            if (!(s.black & bit)) {
                                continue;
                            }
                            // (2b) must start after we have opened F+1
                            if (s.finalized < F + 1) {
                                // This scan started before we opened all the
                                // reports that could contain k-gray-allocated
                                // objects
                                
                                // We can't delete scans unilaterally, but we
                                // signal it is of no interest to k at least
                                s.black &= ~bit;
                                continue;
                            }
                            if (s.before <= _shade_most_recent[k]) {
                                // The most recent k-gray happened after this
                                // scan began
                                s.black &= ~bit;
                                continue;
                            }
                            if (_finalized < s.after) {
                                // We haven't yet opened all the reports we
                                // need to trust this epoch
                                continue;
                            }

                            // SUCCESS
                            
                            // During this scan, the mutators made no new gray
                            // objects, and shaded no white objects gray.  They
                            // made only black objects.
                            
                            // The collector scanned all white and gray objects,
                            // turning some white to black and all gray to
                            // black.
                            
                            // There are now only white objects and black objects.
                            
                            kstate[k] = { SWEEPING, E, 0 };

                            // We can now safely clear all the scan history
                            for (auto& t : _scan_history) {
                                t.black &= ~bit;
                            }
                            // TODO: don't start over; use iterators and clear
                            // into the future

                            break; // from loop over _scan_history
                        }
                    }
                        break; // from switch
                        
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
                        // Wait until we have opened all reports that have
                        // objects that might have encountered a k-black mutator.
                        if (_finalized < F + 2)
                            break;
                        // All mutators are now white; k-white is stable
                        kstate[k] = { CLEARING, E, 0 };
                    } break;
                        
                    case CLEARING:
                        // Wait for at least one scan to complete
                        if (!kstate[k].scans)
                            break;
                        // All objects are now k-white
                        kstate[k] = { UNUSED, E, 0 };
                        break;
                        
                } // switch kphase
                
            } // for k
            
            
            // We've cleared the history by bit, erase the entries that are now
            // trivial
            std::erase_if(_scan_history, [](auto const& x) {
                return !x.black;
            });
            
            // TODO: Rather than writing everywhere, we can probably filter with
            // a mask, and that mask is just black_for_allocation
            
            
            
            
            
            // Derive bitmasks.  Once we're confident, we can rely on
            // progression and apply deltas
            
            for (int k = 0; k != 16; ++k) {
                uint16_t bit = 1 << k;
                
                switch (kstate[k].kphase) {
                    case UNUSED:
                        _gray_for_allocation &= ~bit;
                        _black_for_allocation &= ~bit;
                        _mask_for_deleting &= ~bit;
                        _mask_for_clearing &= ~bit;
                        _debug_assert_white |= bit;
                        _debug_assert_nonblack |= bit;
                        break;
                    case GRAY_PUBLISHED:
                        _gray_for_allocation |= bit;
                        _black_for_allocation &= ~bit;
                        _mask_for_deleting &= ~bit;
                        _mask_for_clearing &= ~bit;
                        _debug_assert_white &= ~bit;
                        _debug_assert_nonblack |= bit;
                        break;
                    case BLACK_PUBLISHED:
                        _gray_for_allocation |= bit;
                        _black_for_allocation |= bit;
                        _mask_for_deleting &= ~bit;
                        _mask_for_clearing &= ~bit;
                        _debug_assert_white &= ~bit;
                        _debug_assert_nonblack &= ~bit;
                        break;
                    case SWEEPING:
                        _gray_for_allocation |= bit;
                        _black_for_allocation |= bit;
                        _mask_for_deleting |= bit;
                        _mask_for_clearing &= ~bit;
                        _debug_assert_white &= ~bit;
                        _debug_assert_nonblack &= ~bit;
                        break;
                    case WHITE_PUBLISHED:
                        _gray_for_allocation &= ~bit;
                        _black_for_allocation &= ~bit;
                        _mask_for_deleting &= ~bit;
                        _mask_for_clearing &= ~bit;
                        _debug_assert_white &= ~bit;
                        _debug_assert_nonblack &= ~bit;
                        break;
                    case CLEARING:
                        _gray_for_allocation &= ~bit;
                        _black_for_allocation &= ~bit;
                        _mask_for_deleting &= ~bit;
                        _mask_for_clearing |= bit;
                        _debug_assert_white &= ~bit;
                        _debug_assert_nonblack &= ~bit;
                        break;
                }
                
                
            }
            
        } // void Collector::try_advance_collection_phases()

        
        void violation(GarbageCollected const* object, uint16_t gray, uint16_t black, int32_t count) {
                        
            uint16_t a = black & ~gray;
            uint16_t b = (gray | black) & _debug_assert_white;
            uint16_t c = black & _debug_assert_nonblack;
            uint16_t d = (gray ^ black) & _mask_for_deleting;
            
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
                   _mask_for_deleting,
                   _mask_for_clearing,
                   _debug_assert_white);
            
            
            __builtin_trap();
        }

        void collector_scans() {

#pragma mark Scan all known objects

            Bag<const GarbageCollected*> survivors;

            size_t trace_count = 0;
            size_t mark_count = 0;
            size_t delete_count = 0;
            size_t scan_count = 0;
            auto t0 = std::chrono::steady_clock::now();

            assert(_graystack.debug_is_empty());
            assert(survivors.debug_is_empty());
            assert(global_children.debug_is_empty());

            // validate state:

            assert((_mask_for_deleting & _mask_for_clearing) == 0);
            assert((_mask_for_clearing & _gray_for_allocation) == 0);
            assert((_mask_for_clearing & _black_for_allocation) == 0);

            for (GarbageCollected const* object : _known_objects) {
                uint16_t gray  = object->_gray.load_relaxed();
                uint16_t black = object->_black;
                int32_t count = object->_count.load_relaxed();
                violation(object, gray, black, count);
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
                            after_gray = (before_gray | parent_black) & ~_mask_for_clearing;
                            if (after_gray == before_gray)
                                break;
                            if (child->_gray.compare_exchange_weak_relaxed_relaxed(before_gray,
                                                                                   after_gray))
                                break;
                        }
                        uint16_t mark_black = after_gray & _black_for_allocation;
                        uint16_t after_black = (before_black | mark_black) & ~_mask_for_clearing;
                        child->_black = after_black;
                        violation(child, after_gray, after_black, reference_count);
                        // uint16_t did_set_gray  = ~before_gray  & after_gray;
                        uint16_t did_set_black = ~before_black & after_black;
                        if (did_set_black) {
                            ++mark_count;
                            _graystack.push(child);
                        }
                    }
                }

                // Resume scanning each object in turn.
                // (Many will have already been processed by tracing)

                const GarbageCollected* object = nullptr;
                if (!_known_objects.try_pop(object))
                    break;
                assert(object);
                ++scan_count;

                
                // Process the object:

                // If root, and gray is active, make it gray
                // If in clearing mask, clear it
                                
                int32_t reference_count = object->_count.load_relaxed();
                uint16_t root_gray = reference_count ? _gray_for_allocation : 0;
                uint16_t before_gray = object->_gray.load_relaxed();
                uint16_t before_black = object->_black;
                uint16_t after_gray;
                for (;;) {
                    violation(object, before_gray, before_black, reference_count);
                    after_gray = (before_gray | root_gray) & ~_mask_for_clearing;
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
                uint16_t after_black = (before_black | mark_black) & ~_mask_for_clearing;
                object->_black = after_black;
                violation(object, after_gray, after_black, reference_count);

                // uint16_t did_set_gray  = ~before_gray  & after_gray;
                uint16_t did_set_black = ~before_black & after_black;

                if (did_set_black) {
                    ++trace_count;
                    _graystack.push(object);
                }
                
                if (_mask_for_deleting && !(_mask_for_deleting & after_gray)) {
                    delete object;
                    ++delete_count;
                } else {
                    survivors.push(std::move(object));
                }

            } // loop until no objects
            
            assert(_graystack.debug_is_empty());
            assert(_known_objects.debug_is_empty());
            assert(global_children.c.empty());
            _known_objects.swap(survivors);
            
            auto t1 = std::chrono::steady_clock::now();
            
            printf("C0: scanned=%zd,marked=%zd,deleted=%zd in %.3gs\n",
                   scan_count,
                   trace_count + mark_count,
                   delete_count,
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
    }




} // namespace wry






// LEGACY

namespace wry {

    const HeapString* HeapString::make(size_t hash, std::string_view view) {
        // TODO: this should intern via a global string ctrie; for now we
        // allocate a fresh HeapString every call.  Original sketch:
        //   return global_collector->string_ctrie->find_or_emplace(_ctrie::Query{hash, view});
        size_t n = view.size();
        size_t bytes = sizeof(HeapString) + n;
        void* raw = GarbageCollected::operator new(bytes);
        std::memset(raw, 0, bytes);
        HeapString* a = new(raw) HeapString;
        a->_hash = hash;
        a->_size = n;
        std::memcpy(a->_bytes, view.data(), n);
        return a;
    }


}


