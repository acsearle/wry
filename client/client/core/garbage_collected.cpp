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

namespace wry {

#pragma mark - Forward declarations

    using namespace detail;

#pragma mark - Global and thread_local variables

    constinit thread_local uint16_t _thread_local_gray_for_allocation;
    constinit thread_local uint16_t _thread_local_black_for_allocation;
    constinit thread_local uint16_t _thread_local_gray_did_shade;
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;

    GarbageCollected::GarbageCollected()
    : _gray{_thread_local_gray_for_allocation}
    , _black{_thread_local_black_for_allocation}
    , _count{0} {
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.
        _thread_local_new_objects.push(this);
    }

    GarbageCollected::GarbageCollected(GarbageCollected::DeferRegistrationTag)
    : _gray{}
    , _black{0}
    , _count{0} {
    }

    void GarbageCollected::_garbage_collected_complete_deferred_registration() const {
        _gray.store(_thread_local_gray_for_allocation, Ordering::RELAXED);
        // SAFETY: plain store into a mutable field is race-free because the
        // object is still visible only to the allocating thread; it will be
        // published to the collector below.
        _black = _thread_local_black_for_allocation;
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
            std::move(_thread_local_new_objects)
        };
        desired->_next = _global_atomic_reports_head.load(Ordering::RELAXED);
        while (!_global_atomic_reports_head.compare_exchange_weak(desired->_next,
                                                                  desired,
                                                                  Ordering::RELEASE,
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

    Report* collector_takes_reports(){
        return _global_atomic_reports_head.exchange(nullptr,
                                                    Ordering::ACQUIRE);
    }







    struct Collector {

        InlineRingBuffer<uint16_t, 4> _gray_history;
        InlineRingBuffer<uint16_t, 4> _black_history;
        InlineRingBuffer<uint16_t, 4> _shade_history;

        Bag<const GarbageCollected*> _known_objects;

        uint16_t _gray_for_allocation = 0;
        uint16_t _black_for_allocation = 0;
        uint16_t _color_in_use = 0;
        uint16_t _mask_for_tracing = 0;
        uint16_t _mask_for_deleting = 0;
        uint16_t _mask_for_clearing = 0;

        Atomic<bool> _is_canceled;

        Stack<const GarbageCollected*> _graystack;

        ~Collector() {
            _known_objects.leak();
        }



        void loop_until_canceled() {

            // Does the collector need to pin?
            // Or just to spy on the epoch?

            epoch::pin_this_thread();
            epoch::Epoch epoch_at_last_change = epoch::allocator_local_state.known;

            printf("C0: garbage collector starts\n");

            while (!_is_canceled.load(Ordering::RELAXED)) {

                epoch::repin_this_thread();


                // Always read all reports
                {
                    uint16_t did_shade = 0;
                    Report* head = collector_takes_reports();
                    while (head) {
                        did_shade |= head->gray_did_shade;
                        _known_objects.splice(std::move(head->allocations));
                        delete std::exchange(head, head->_next);
                    }
                    _shade_history.front() |= did_shade;
                }

                // All of the above ops are benign information gathering.

                // We check if the epoch has changed enough that we can prove
                // that every (active) mutator has adopted the last colors
                // we published.

                // It's important to structure the comparison to work when
                // wrapping occurs
                if (epoch::allocator_local_state.known - epoch_at_last_change < 2) {
                    // TODO: Best way to sleep or wait here
                    epoch::unpin_this_thread();
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    epoch::pin_this_thread();
                    continue;
                }

                // The epoch has advanced by at least two since we published
                //
                // This means every mutator has repinned and then loaded the
                // color at least once
                //
                // Every (active) mutator has now seen the latest color we
                // published, so we can proceed to advance the collection

                // write the tricolor state
                // release-acquire epoch
                // note the epoch

                // write a report on the old state
                // release-acquire epoch
                // read the trcolor state

                // release-acquire epoch
                // read the report state

                // This establishes a release sequence.  The epoch properties
                // guarantee advancement only when nobody is in the old epoch,
                // which means that when the epoch has advanced by two we have
                // everybody agreeing that the original epoch is in the past

                try_advance_collection_phases();

                _shade_history.push_front(0);

                // Publish the new colors
                _gray_history.push_front(_gray_for_allocation);
                _black_history.push_front(_black_for_allocation);
                Color color = {
                    .gray = _gray_for_allocation,
                    .black = _black_for_allocation
                };
                _global_atomic_color_for_allocation.store(color, Ordering::RELAXED);

                epoch::repin_this_thread();

                epoch_at_last_change = epoch::allocator_local_state.known;

                // Visit every object to trace and sweep them.
                scan();

            } // while (!_is_cancelled.load(Ordering::RELAXED))

            epoch::unpin_this_thread();

        } // void Collector::loop_until_canceled()


        void try_advance_collection_phases() {

            // All threads have now report shading up to the last(?) epoch
            //
            // We can now try to advance the state of each of the collections
            // through their several phases

            uint16_t old_mask_for_deleting = _mask_for_deleting;
            uint16_t old_mask_for_clearing = _mask_for_clearing;

            {
                // When all threads have acknowledged k-gray, publish k-black
                _black_for_allocation |= _gray_history[0] & ~_gray_history[1];
            }

            {
                // When all threads have acknowledged k-black, start tracing
                _mask_for_tracing |= _black_history[0] & ~_black_history[1];
            }

            {
                // When we can prove all threads have made no new k-gray
                // during a whole sweep
                uint16_t color_is_stable = _mask_for_tracing;
                color_is_stable &= ~_shade_history[0];
                color_is_stable &= ~_shade_history[1];
                color_is_stable &= ~_shade_history[2];
                // Stop tracing these colors
                _mask_for_tracing &= ~color_is_stable;
                // Start deleting these whites
                _mask_for_deleting = color_is_stable;
            }

            {
                // When we have deleted k-white, unpublish k-gray and k-black
                // together
                assert(is_subset_of(old_mask_for_deleting, _gray_for_allocation));
                assert(is_subset_of(old_mask_for_deleting, _black_for_allocation));
                _gray_for_allocation  &= ~old_mask_for_deleting;
                _black_for_allocation &= ~old_mask_for_deleting;
            }

            {
                // When all threads stop using k-gray and k-black, clear all k-bits
                _mask_for_clearing = ~_gray_history[1] & _gray_history[2];
                // We need to wait two cycles so that the collector
                // has received objects allocated k-white by a leading
                // mutator but shaded gray by a trailing mutator
                // This means that we will clear objects in all k-states:
                // recently allocated white, old allocated black, and
                // recently allocated white and shaded black by leading and
                // trailling mutators
            }

            {
                _color_in_use &= ~old_mask_for_clearing;
                uint16_t new_bit = (_color_in_use + 1) & ~_color_in_use;
                _gray_for_allocation |= new_bit;
                _color_in_use       |= new_bit;
            }

        } // void Collector::advance_state()


        void scan() {

#pragma mark Scan all known objects

            Bag<const GarbageCollected*> survivors;

            size_t trace_count = 0;
            size_t mark_count = 0;
            size_t delete_count = 0;
            auto t0 = std::chrono::steady_clock::now();

            assert(_graystack.debug_is_empty());
            assert(survivors.debug_is_empty());
            assert(global_children.c.empty());

            // validate state:

            assert(is_subset_of(_gray_for_allocation, _color_in_use));
            assert(is_subset_of(_black_for_allocation, _color_in_use));

            assert(is_subset_of(_mask_for_tracing, _color_in_use));
            assert(is_subset_of(_mask_for_deleting, _color_in_use));
            assert(is_subset_of(_mask_for_clearing, _color_in_use));

            assert(is_subset_of(_mask_for_tracing, _gray_for_allocation));
            assert(is_subset_of(_mask_for_tracing, _black_for_allocation));
            assert((_mask_for_tracing & _mask_for_deleting) == 0);
            assert((_mask_for_tracing & _mask_for_clearing) == 0);
            assert((_mask_for_deleting & _mask_for_clearing) == 0);
            assert((_mask_for_clearing & _gray_for_allocation) == 0);
            assert((_mask_for_clearing & _black_for_allocation) == 0);

            // dump(_gray_for_allocation);
            // dump(_black_for_allocation);
            // dump(_mask_for_tracing);
            // dump(_mask_for_deleting);
            // dump(_mask_for_clearing);

            printf("C0: Start scanning %zd objects with\n"
                   "              trace mask %04x\n"
                   "             delete mask %04x\n"
                   "              clear mask %04x\n"
                   "     gray_for_allocation %04x\n"
                   "    black_for_allocation %04x\n",
                   _known_objects.debug_size(),
                   _mask_for_tracing,
                   _mask_for_deleting,
                   _mask_for_clearing,
                   _gray_for_allocation,
                   _black_for_allocation);

            // While any objects are unprocessed
            for (;;) {

                // Depth-first recusively trace all the known children

                const GarbageCollected* parent = nullptr;
                while (_graystack.try_pop(parent)) {
                    assert(parent);
                    uint16_t parent_gray = parent->_gray.load(Ordering::RELAXED);
                    parent->_garbage_collected_scan();
                    const GarbageCollected* child = nullptr;
                    while (global_children.try_pop(child)) {
                        int32_t reference_count = child->_count.load(Ordering::RELAXED);
                        uint16_t rooted = reference_count ? _mask_for_tracing : 0;
                        uint16_t before_gray = child->_gray.load(Ordering::RELAXED);
                        uint16_t after_gray;
                        do {
                            assert(is_subset_of(before_gray, _color_in_use));
                            after_gray = (before_gray
                                          | (parent_gray & _mask_for_tracing)
                                          | rooted) & ~_mask_for_clearing;
                            assert(is_subset_of(after_gray, _color_in_use));
                        } while ((after_gray != before_gray) &&
                                 !child->_gray.compare_exchange_weak(before_gray,
                                                                     after_gray,
                                                                     Ordering::RELAXED,
                                                                     Ordering::RELAXED));
                        // SAFETY: only the collector writes _black after
                        // registration, so a plain read-modify-write is safe.
                        uint16_t before_black = child->_black;
                        uint16_t mark = after_gray & _mask_for_tracing;
                        uint16_t after_black = (before_black | mark) & ~_mask_for_clearing;
                        assert(is_subset_of(after_black, _color_in_use));
                        child->_black = after_black;
                        uint16_t did_set_gray  = ~before_gray  & after_gray;
                        uint16_t did_set_black = ~before_black & after_black;
                        if (did_set_gray | did_set_black) {
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
                // Process the object.
                // Depending on phase, change the k-coloration
                // - k-mark: k-gray -> k-black, and enqueue for tracing
                // - k-clear: k-* -> k-white
                int32_t reference_count = object->_count.load(Ordering::RELAXED);
                uint16_t rooted = reference_count ? _mask_for_tracing : 0;
                uint16_t before_gray = object->_gray.load(Ordering::RELAXED);
                uint16_t after_gray;
                for (;;) {
                    assert(is_subset_of(before_gray, _color_in_use));
                    after_gray = (before_gray | rooted) & ~_mask_for_clearing;
                    assert(is_subset_of(after_gray, _color_in_use));
                    if (after_gray == before_gray)
                        // No change to write
                        // TODO: Is this actually an optimization?
                        break;
                    if (object->_gray.compare_exchange_weak(before_gray,
                                                            after_gray,
                                                            Ordering::RELAXED,
                                                            Ordering::RELAXED))
                        // Changed the gray bits
                        break;
                    // Compare exchange failed, start over
                }
                // SAFETY: only the collector writes _black after registration.
                uint16_t before_black = object->_black;
                // Mark uses before_gray (not after_gray) to match the
                // pre-split semantics: newly-rooted objects become k-gray
                // this cycle and k-black on the next.
                uint16_t mark = before_gray & _mask_for_tracing;
                uint16_t after_black = (before_black | mark) & ~_mask_for_clearing;
                assert(is_subset_of(after_black, _color_in_use));
                object->_black = after_black;

                uint16_t did_set_gray  = ~before_gray  & after_gray;
                uint16_t did_set_black = ~before_black & after_black;
                assert((did_set_gray == 0) || reference_count); // Never k-white -> k-gray
                bool must_trace = did_set_black != 0; // If k-gray -> k-black
                if (must_trace) {
                    ++trace_count;
                    _graystack.push(object);
                }
                bool is_not_gray = ((before_black & _mask_for_deleting)
                                    == (before_gray & _mask_for_deleting));
                if ((_mask_for_deleting == 0) || (before_gray & _mask_for_deleting)) {
                    // k-reachable
                    if (!is_not_gray) {
                        dump(before_gray);
                        dump(before_black);
                        dump(after_gray);
                        dump(after_black);
                        dump(did_set_gray);
                        dump(did_set_black);
                        dump(before_gray & _mask_for_deleting);
                        object->_garbage_collected_debug();
                        abort();
                    }
                    survivors.push(std::move(object));
                } else {
                    // k-unreachable
                    if (must_trace) {
                        dump(before_gray);
                        dump(before_black);
                        dump(after_gray);
                        dump(after_black);
                        dump(did_set_gray);
                        dump(did_set_black);
                        dump(before_gray & _mask_for_deleting);
                        abort();
                    }
                    // Must not be k-gray; k-gray would imply not k-stable
                    assert(are_gray(before_gray & _mask_for_deleting,
                                    before_black & _mask_for_deleting) == 0);
                    delete object;
                    ++delete_count;
                }

            } // loop until no objects

            assert(_graystack.debug_is_empty());
            assert(_known_objects.debug_is_empty());
            assert(global_children.c.empty());
            _known_objects = std::move(survivors);

             auto t1 = std::chrono::steady_clock::now();
            
             printf("C0:     marked %zd\n", trace_count + mark_count);
             printf("C0:     deleted %zd\n", delete_count);
             printf("C0:     in %.3gs\n", std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);

        } // void Collector::scan()

    }; // struct Collector

    static constinit Collector collector = {};

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


