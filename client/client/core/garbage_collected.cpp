//
//  garbage_collected.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cstdlib>
#include <cstdio>
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

#define dump(X) printf("C0.%d: %04x = " #X "\n", __LINE__, (X));

namespace wry::bump {
    thread_local State this_thread_state{};
}

namespace wry {

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
    , _debug_allocation_epoch{epoch::allocator_local_state.known.raw}
    {
        // Allocation may produce gray objects for some states
        _thread_local_gray_did_shade |= _thread_local_gray_for_allocation & ~_thread_local_black_for_allocation;
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.
        _thread_local_new_objects.push(this);
    }

    void GarbageCollected::_garbage_collected_shade() const {
        const uint16_t gray = _thread_local_gray_for_allocation;
        const uint16_t before = _gray.fetch_or(gray, Ordering::RELAXED);
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

        Report* _next = nullptr;
        uint16_t gray_did_shade = 0;
        Bag<const GarbageCollected*> allocations;
        uint16_t debug_gray_for_allocation;
        uint16_t debug_black_for_allocation;
        epoch::Epoch debug_epoch;

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
            nullptr,
            std::exchange(_thread_local_gray_did_shade, 0),
            std::move(_thread_local_new_objects),
            _thread_local_gray_for_allocation,
            _thread_local_black_for_allocation,
            epoch::allocator_local_state.known
        };
        // SAFETY: We perform a RELAXED write.  It's not safe for the collector
        // to dereference this pointer until the epoch has advanced.
        desired->_next = _global_atomic_reports_head.load(Ordering::RELAXED);
        while (!_global_atomic_reports_head.compare_exchange_weak(desired->_next,
                                                                  desired,
                                                                  Ordering::RELAXED,
                                                                  Ordering::RELAXED))
            ;
    }

    void mutator_pin() {
        epoch::pin_this_thread();
        Color color = _global_atomic_color_for_allocation.load(Ordering::RELAXED);
        _thread_local_gray_for_allocation = color.gray;
        _thread_local_black_for_allocation = color.black;
    }

    void mutator_repin() {
        _mutator_publishes_report();
        epoch::repin_this_thread();
        Color color = _global_atomic_color_for_allocation.load(Ordering::RELAXED);
        _thread_local_gray_for_allocation = color.gray;
        _thread_local_black_for_allocation = color.black;
    }

    void mutator_unpin() {
        _mutator_publishes_report();
        epoch::unpin_this_thread();
    }
    
    
    using epoch::Epoch;
    
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
    
    struct ShadeRecord {
        epoch::Epoch when;
        uint16_t gray;
    };
    
    struct ScanRecord {
        epoch::Epoch before;
        epoch::Epoch after;
        uint16_t black;
        epoch::Epoch finalized;
    };
    
        
    struct Collector {
        
        // InlineRingBuffer<uint16_t, 8> _history_of_mask_for_tracing;

        Bag<const GarbageCollected*> _known_objects;

        uint16_t _gray_for_allocation = 0;
        uint16_t _black_for_allocation = 0;
        uint16_t _mask_for_deleting = 0;
        uint16_t _mask_for_clearing = 0;
        
        uint16_t _debug_assert_white;
        uint16_t _debug_assert_nonblack;

        Atomic<bool> _is_canceled;

        Stack<const GarbageCollected*> _graystack;
        
        
        
        std::deque<EmbargoedReport> _embargoed_reports;
        
        Epoch _finalized{0xFFF0};
        
        std::deque<ShadeRecord> _shade_history;
        std::deque<ScanRecord> _scan_history;
        
                                
        ~Collector() {
            _known_objects.leak();
        }

        void collector_takes_reports(){
            Report* head =  _global_atomic_reports_head.exchange(nullptr,
                                                                 Ordering::RELAXED);
            assert(epoch::allocator_local_state.is_pinned);
            epoch::Epoch E = epoch::allocator_local_state.known;
            _embargoed_reports.emplace_back(E, head);
        }
        
        void collector_reads_reports() {
            assert(epoch::allocator_local_state.is_pinned);
            Epoch E = epoch::allocator_local_state.known;
            auto iterator = _embargoed_reports.begin();
            while (iterator != _embargoed_reports.end()) {
                Epoch F = iterator->received;
                // Reports were received at F
                // Mutators still might be at F-1
                // The last reports we can be sure we have got are from F-2
                Epoch G = F - 2;
                if (E >= F + 3) {
                    printf("final %04x < %04x (now) skew %04x\n", G.raw, E.raw, E - G);
                    Report* head = iterator->report;
                    while (head) {
                        Epoch H = Epoch{head->debug_epoch};
                        // Report was received in epoch F.
                        // printf("Report skew, %d\n", H - F);
                        assert(_finalized < H);
                        assert(H <= F + 1);
                        
                        _known_objects.splice(std::move(head->allocations));
                        _shade_history.emplace_back(H, head->gray_did_shade);
                        delete std::exchange(head, head->_next);
                    }
                    assert(G > _finalized);
                    _finalized = G;
                    iterator = _embargoed_reports.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }

        void loop_until_canceled() {
            
            epoch::pin_this_thread();
            assert(epoch::allocator_local_state.is_pinned);
            epoch::Epoch epoch_at_last_change = epoch::allocator_local_state.known;

            printf("C0: garbage collector starts\n");

            while (!_is_canceled.load(Ordering::RELAXED)) {
                
                assert(epoch::allocator_local_state.is_pinned);
                epoch::Epoch current_epoch = epoch::allocator_local_state.known;

                if (current_epoch != epoch_at_last_change) {
                    
                    printf("EPOCH ADVANCEMENT: %04x\n", current_epoch.raw - epoch_at_last_change.raw);
                                        
                    collector_takes_reports();
                    collector_reads_reports();
                    
                    try_advance_collection_phases();
                    
                    Color color = {
                        .gray = _gray_for_allocation,
                        .black = _black_for_allocation
                    };
                    _global_atomic_color_for_allocation.store(color, Ordering::RELAXED);
                    
                    epoch_at_last_change = current_epoch;

                    for (int k = 0; k != 16; ++k) {
                        kstate[k].scans++;
                    }
                }
                
                assert(epoch::allocator_local_state.is_pinned);
                Epoch A{epoch::allocator_local_state.known};
                epoch::unpin_this_thread();
                
                collector_scans();
                
                //std::this_thread::sleep_for(std::chrono::milliseconds(20));
                
                epoch::pin_this_thread();
                assert(epoch::allocator_local_state.is_pinned);
                Epoch B{epoch::allocator_local_state.known};
                _scan_history.emplace_back(A, B, _black_for_allocation, _finalized);

                // epoch::repin_this_thread();
                

            } // while (!_is_cancelled.load(Ordering::RELAXED))

        } // void Collector::loop_until_canceled()
        


        void try_advance_collection_phases() {
            
            assert(epoch::allocator_local_state.is_pinned);
            epoch::Epoch E = epoch::allocator_local_state.known;

            bool first = true;
            
            
            // Compute transitions
            
            for (int k = 0; k != 16; ++k) {
                uint16_t bit = 1 << k;
                
                switch (kstate[k].kphase) {
                        
                    case UNUSED:
                        if (!first)
                            break;
                        first = false;
                        kstate[k] = { GRAY_PUBLISHED, E, 0 };
                        break;
                        
                    case GRAY_PUBLISHED:
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
                        // We need to have all reports from F + 1
                        if (_finalized < F + 1)
                            break;
                        
                        // Condition (2)
                        bool happy = false;
                        for (auto& s : _scan_history) {
                            // must mark k-black
                            if (!(s.black & bit)) {
                                continue;
                            }
                            // must start after we have opened F+1
                            if (s.finalized < F + 1) {
                                // this scan started before we opened the last k-gray-allocated reports
                                s.black &= ~bit;
                                continue;
                            }
                            if (_finalized < s.after) {
                                // we haven't yet got all the gray reports we need
                                continue;
                            }
                            // check if there is any gray in this epoch
                            uint16_t gray{};
                            for (auto& g : _shade_history) {
                                if ((g.when >= s.before) && (g.when <= s.after)) {
                                    gray |= g.gray;
                                }
                            }
                            if (gray & k) {
                                // no good; shading happened during the scan
                                continue;
                            }
                            happy = true;
                            break;
                        }
                        if (!happy)
                            break;
                        
                        
                        // zero the relevant bits so we can later drain
                        for (auto& s : _scan_history) {
                            s.black &= ~bit;
                        }
                        for (auto& g : _shade_history) {
                            g.gray &= ~bit;
                        }
                        
                        

                        //
                        // Timing argument:
                        //
                        //   T = kstate[k].since is the iteration at which we
                        //   entered TRACING. By then T_b+2 = T (or earlier),
                        //   so the latest mutator epoch that could have
                        //   allocated k-gray is K = T-1.
                        //
                        //   Reports from K = T-1 are taken at iteration
                        //   ≥ K+1 = T (no skip). Their embargo lifts at
                        //   take+3 ≥ T+3, so the latest k-gray object is
                        //   spliced in by iteration T+3 and traced by the
                        //   scan that follows. (1) and (2) are satisfied by
                        //   E ≥ T+3.
                        //
                        //   did_shade(X) for some mutator epoch X is final
                        //   at iteration X+5 (the latest take is at iteration
                        //   X+2 in the no-skip case, embargoed to X+5).
                        //   So at iteration E we can trust did_shade(X) for
                        //   X ≤ E-5.
                        //
                        //   For (3) we want at least one finalized
                        //   post-TRACING epoch in the window, i.e. some X
                        //   with T+1 ≤ X ≤ E-5. The range is non-empty when
                        //   E ≥ T+6.
                        //
                        //   We use [T+1, E-5] (skipping X = T itself,
                        //   because in-scan write barriers running
                        //   concurrently with the first tracing scan can
                        //   legitimately set did_shade(T) for bit k while
                        //   visiting still-untouched k-white reachables).
//
//                        Epoch T = kstate[k].since;
//                        if (E - T < 6)
//                            break;
//
//                        bool any_shade = false;
//                        for (Epoch X = T + 1; X <= E - 5; X = X + 1) {
//                            //if (log_at(X).did_shade & bit) {
//                            //    any_shade = true;
//                            //    break;
//                            //}
//                        }
//                        //if (any_shade)
//                        //    break;
//                        
//                        // HACK:
//                        if (E < kstate[k].since + 10)
//                            break;
//                        if (kstate[k].scans < 10)
//                            break;

                        kstate[k] = { SWEEPING, E, 0 };
                        break;
                    }
                        
                    case SWEEPING:
                        if (!kstate[k].scans)
                            break;
                        kstate[k] = { WHITE_PUBLISHED, E, 0 };
                        break;
                        
                    case WHITE_PUBLISHED: {

                        // Let F be epoch transition happened.
                        // In F+2 all mutators are white.
                        // In F+1 some mutators are black
                        // When F+1 and F+2 coexist, black mutators might shade
                        // white allocations
                        // Once F+2 is finalized we will have all new objects
                        // that could be nonwhite
                        
                        Epoch F = kstate[k].since;
                        if (_finalized < F + 2)
                            break;
                        
                        // printf("WHITE skew %04x\n", E - F);
                        
                        kstate[k] = { CLEARING, E, 0 };
                    } break;
                        
                    case CLEARING:
                        if (!kstate[k].scans)
                            break;
                        kstate[k] = { UNUSED, E, 0 };
                        break;
                        
                } // switch kphase
                
                
                // CRITICAL:
                //
                // If we advance many epochs during a scan,
                // then get reports
                // then wait for embargo
                // we don't get the reports until a time that depends on the
                // slip during the scan
                // i.e. the epoch after the scan, not the start of the scan
                // is more relevant.
                
                // If we don't take the report for E+1 until E+10 it won't
                // open until E+15 so the fact we are in E+5 is meaningless

                
            } // for k
            
            
            // We've cleared the history by bit, erase the entries that are now
            // trivial
            {
                std::erase_if(_scan_history, [](auto const& x) {
                    return !x.black;
                });
                std::erase_if(_shade_history, [](auto const& x) {
                    return !x.gray;
                });
            }
            
            
            
            
            // Derive bitmasks
            
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
            
            
            
            
            
            
            
//            // We can now try to advance the state of each of the collections
//            // through their several phases
//
//            uint16_t old_mask_for_deleting = _mask_for_deleting;
//            uint16_t old_mask_for_clearing = _mask_for_clearing;
//
//            {
//                // 0 - now
//                // 1 - was gray
//                // 2 - becomes gray
//                // 3 - was white
//                // When the white-gray transition was far enough in the past
//                uint16_t g1 = log_at(E-1).gray;
//                uint16_t g2 = log_at(E-2).gray;
//                uint16_t g3 = log_at(E-3).gray;
//                uint16_t before = _black_for_allocation;
//                _black_for_allocation |= g1 & g2 & ~g3;
//                printf("_black_for_allocation %04x -> %04x\n", before, _black_for_allocation);
//            }
//
//            {
//                // When the gray-black transition was far enough in the past
//                uint16_t b1 = log_at(E-1).black;
//                uint16_t b2 = log_at(E-2).black;
//                uint16_t b3 = log_at(E-3).black;
//                //auto before = _mask_for_tracing;
//                //_mask_for_tracing |= b1 & b2 & ~b3;
//                //printf("_mask_for_tracing %04x -> %04x\n", before, _mask_for_tracing);
//            }
//
//            {
//                // When we can prove all threads have made no new k-gray
//                // during a whole sweep
//                //uint16_t color_is_stable = _history_of_mask_for_tracing[7];
////                color_is_stable &= ~log_at(E+1).did_shade;
////                color_is_stable &= ~log_at(E).did_shade;
////                color_is_stable &= ~log_at(E-1).did_shade;
////                color_is_stable &= ~log_at(E-2).did_shade;
////                color_is_stable &= ~log_at(E-3).did_shade;
////                color_is_stable &= ~log_at(E-4).did_shade;
////                color_is_stable &= ~log_at(E-5).did_shade;
////                color_is_stable &= ~log_at(E-6).did_shade;
////                color_is_stable &= ~log_at(E-7).did_shade;
//                // Stop tracing these colors
//                //_mask_for_tracing &= ~color_is_stable;
//                //printf("    _mask_for_tracing      -> %04x\n", _mask_for_tracing);
//                //_history_of_mask_for_tracing.push_front(_mask_for_tracing);
//                // Start deleting these whites
//                //auto before = _mask_for_deleting;
//                //_mask_for_deleting = color_is_stable;
//                //printf("_mask_for_deleting %04x -> %04x\n", before, _mask_for_deleting);
//            }
//
//            {
//                // When we have deleted k-white, unpublish k-gray and k-black
//                // together
//                printf("        old_mask_for_deleting %04x\n", old_mask_for_deleting);
//                printf("        _gray_for_allocation %04x\n", _gray_for_allocation);
//                printf("        _black_for_allocation %04x\n", _black_for_allocation);
//                assert(is_subset_of(old_mask_for_deleting, _gray_for_allocation));
//                assert(is_subset_of(old_mask_for_deleting, _black_for_allocation));
//                auto before = _gray_for_allocation;
//                _gray_for_allocation  &= ~old_mask_for_deleting;
//                printf("_gray_for_allocation %04x -> %04x\n", before, _gray_for_allocation);
//                before = _black_for_allocation;
//                _black_for_allocation &= ~old_mask_for_deleting;
//                printf("_black_for_allocation %04x -> %04x\n", before, _black_for_allocation);
//            }
//
//            {
//                // When all threads stop using k-gray and k-black, clear all k-bits
//                uint16_t g1 = log_at(E-1).gray;
//                uint16_t g2 = log_at(E-2).gray;
//                uint16_t g3 = log_at(E-3).gray;
//                _mask_for_clearing = ~g1 & ~g2 & g3;
//                // We need to wait two cycles so that the collector
//                // has received objects allocated k-white by a leading
//                // mutator but shaded gray by a trailing mutator
//                // This means that we will clear objects in all k-states:
//                // recently allocated white, old allocated black, and
//                // recently allocated white and shaded black by leading and
//                // trailling mutators
//            }
//
//            {
//                _color_in_use &= ~old_mask_for_clearing;
//                uint16_t new_bit = (_color_in_use + 1) & ~_color_in_use;
//                _gray_for_allocation |= new_bit;
//                printf("    _gray_for_allocation      -> %04x\n", _gray_for_allocation);
//                _color_in_use       |= new_bit;
//            }

        } // void Collector::advance_state()

        
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
            
            bool is_pinned = epoch::allocator_local_state.is_pinned;
            epoch::Epoch E = epoch::allocator_local_state.known;
            
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
            assert(global_children.c.empty());

            // validate state:

//            assert(is_subset_of(_gray_for_allocation, _color_in_use));
//            assert(is_subset_of(_black_for_allocation, _color_in_use));
//
//            assert(is_subset_of(_mask_for_tracing, _color_in_use));
//            assert(is_subset_of(_mask_for_deleting, _color_in_use));
//            assert(is_subset_of(_mask_for_clearing, _color_in_use));
//
//            assert(is_subset_of(_mask_for_tracing, _gray_for_allocation));
//            assert(is_subset_of(_mask_for_tracing, _black_for_allocation));
//            assert((_mask_for_tracing & _mask_for_deleting) == 0);
//            assert((_mask_for_tracing & _mask_for_clearing) == 0);
            assert((_mask_for_deleting & _mask_for_clearing) == 0);
            assert((_mask_for_clearing & _gray_for_allocation) == 0);
            assert((_mask_for_clearing & _black_for_allocation) == 0);

            // dump(_gray_for_allocation);
            // dump(_black_for_allocation);
            // dump(_mask_for_tracing);
            // dump(_mask_for_deleting);
            // dump(_mask_for_clearing);

//            printf(
//                   "C0: Start scanning %zd objects with\n"
//                   "     gray_for_allocation %04x\n"
//                   "    black_for_allocation %04x\n"
//                   "       mask_for_deleting %04x\n"
//                   "       mask_for_clearing %04x\n"
//                   "      debug_assert_white %04x\n"
//                   ,
//                   _known_objects.debug_size(),
//                   _gray_for_allocation,
//                   _black_for_allocation,
//                   _mask_for_deleting,
//                   _mask_for_clearing,
//                   _debug_assert_white);
//            
//            for (int k = 0; k != 16; ++k) {
//                printf("    [%02d] %d \"%s\"\n", k, kstate[k].kphase, _KPhase_names[kstate[k].kphase]);
//            }
            
            for (GarbageCollected const* object : _known_objects) {
                uint16_t gray  = object->_gray.load(Ordering::RELAXED);
                uint16_t black = object->_black;
                int32_t count = object->_count.load(Ordering::RELAXED);
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
                        uint16_t before_gray = child->_gray.load(Ordering::RELAXED);
                        uint16_t before_black = child->_black;
                        int32_t reference_count = child->_count.load(Ordering::RELAXED);
                        violation(child, before_gray, before_black, reference_count);
                        uint16_t after_gray;
                        for (;;) {
                            after_gray = (before_gray | parent_black) & ~_mask_for_clearing;
                            if (after_gray == before_gray)
                                break;
                            if (child->_gray.compare_exchange_weak(before_gray,
                                                                   after_gray,
                                                                   Ordering::RELAXED,
                                                                   Ordering::RELAXED))
                                break;
                        }
                        uint16_t mark_black = after_gray & _black_for_allocation;
                        uint16_t after_black = (before_black | mark_black) & ~_mask_for_clearing;
                        child->_black = after_black;
                        violation(child, after_gray, after_black, reference_count);
                        uint16_t did_set_gray  = ~before_gray  & after_gray;
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
                                
                int32_t reference_count = object->_count.load(Ordering::RELAXED);
                uint16_t root_gray = reference_count ? _gray_for_allocation : 0;
                uint16_t before_gray = object->_gray.load(Ordering::RELAXED);
                uint16_t before_black = object->_black;
                uint16_t after_gray;
                for (;;) {
                    violation(object, before_gray, before_black, reference_count);
                    after_gray = (before_gray | root_gray) & ~_mask_for_clearing;
                    if (after_gray == before_gray)
                        break;
                    if (object->_gray.compare_exchange_weak(before_gray,
                                                            after_gray,
                                                            Ordering::RELAXED,
                                                            Ordering::RELAXED))
                        break;
                    // Compare exchange failed, start over
                }
                
                // If gray, and black is active, make it black
                // If in clearing mask, clear it
                
                uint16_t mark_black = after_gray & _black_for_allocation;
                uint16_t after_black = (before_black | mark_black) & ~_mask_for_clearing;
                object->_black = after_black;
                violation(object, after_gray, after_black, reference_count);

                uint16_t did_set_gray  = ~before_gray  & after_gray;
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
            
             printf("C0:     scanned %zd\n", scan_count);
             printf("C0:     marked %zd\n", trace_count + mark_count);
             printf("C0:     deleted %zd\n", delete_count);
             printf("C0:     in %.3gs\n", std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);

        } // void Collector::scan()

    }; // struct Collector

    static Collector collector = {};

    void collector_run_on_this_thread() {
        pthread_setname_np("C0");
        collector.loop_until_canceled();
    }

    void collector_cancel() {
        collector._is_canceled.store(true, Ordering::RELAXED);
    }

    void mutator_overwrote(const GarbageCollected* a) {
        if (a) {
            a->_garbage_collected_shade();
        }
    }




} // namespace wry






// LEGACY

namespace wry {

    const HeapString* HeapString::make(size_t hash, std::string_view view) {
        abort();
#if 0
        return global_collector->string_ctrie->find_or_emplace(_ctrie::Query{hash, view});
#endif
    }


}


