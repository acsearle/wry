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
#include "channel.hpp"
#include "epoch_allocator.hpp"
#include "garbage_collected.hpp"
#include "HeapString.hpp"
#include "stack.hpp"
#include "tagged_ptr.hpp"
#include "utility.hpp"
#include "value.hpp"

#include "test.hpp"

#define dump(X) printf("C0.%d: %016llx = " #X "\n", __LINE__, (X));
namespace wry {
    
    // TODO: are these methods better off being abstract?
        
    void GarbageCollected::_garbage_collected_debug() const {
        abort();
    }
                
} // namespace wry



namespace wry {
    
#pragma mark - Forward declarations
        
    using namespace detail;
    
#pragma mark - Global and thread_local variables
        
    constinit thread_local Color _thread_local_color_for_allocation;
    constinit thread_local Color _thread_local_color_did_shade;
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;
    
    GarbageCollected::GarbageCollected()
    : _color(_thread_local_color_for_allocation) {
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.
        _thread_local_new_objects.push(this);
    }
    
    GarbageCollected::GarbageCollected(GarbageCollected::DeferRegistrationTag)
    : _color{} {
    }
    
    void GarbageCollected::_garbage_collected_complete_deferred_registration() const {
        _color.store(_thread_local_color_for_allocation, Ordering::RELAXED);
        _thread_local_new_objects.push(this);
    }
    
    void GarbageCollected::_garbage_collected_shade() const {
        const Color color_for_shade = _thread_local_color_for_allocation & LOW_MASK;
        const Color before = _color.fetch_or(color_for_shade, Ordering::RELAXED);
        const Color after  =  before | color_for_shade;
        const Color did_shade = (~before) & after;
        _thread_local_color_did_shade |= did_shade;
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
        Color color_did_shade = 0;
        Bag<const GarbageCollected*> allocations;
        
    }; // struct Report

    
    // We expect that these are accessed by each thread on each quiescence,
    // which is a relatively low rate of contention
    
    constinit Atomic<Color> _global_atomic_color_for_allocation = {};
    constinit Atomic<Report*> _global_atomic_reports_head = {};
                
    void _mutator_publishes_report() {
        Report* desired = new Report{
            nullptr,
            std::exchange(_thread_local_color_did_shade, 0),
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
        _thread_local_color_for_allocation = _global_atomic_color_for_allocation.load(Ordering::RELAXED);
    }

    void mutator_repin() {
        _mutator_publishes_report();
        epoch::repin_this_thread();
        _thread_local_color_for_allocation = _global_atomic_color_for_allocation.load(Ordering::RELAXED);
    }
    
    void mutator_unpin() {
        _mutator_publishes_report();
        epoch::unpin_this_thread();
    }
    
    Report* collector_takes_reports(){
        return _global_atomic_reports_head.exchange(nullptr,
                                                    Ordering::ACQUIRE);
    }

        
        

    template<typename T, size_t N, size_t MASK = N-1>
    struct InlineRingBuffer {
        
        static_assert(std::has_single_bit(N), "InlineRingBuffer capacity must be a power of two");
        
        size_t _offset = 0;
        T _array[N] = {};
        
        void push_front(T value) {
            _array[--_offset &= MASK] = value;
        }
        
        const T& operator[](ptrdiff_t i) const {
            assert((0 <= i) && (i < N));
            return _array[(_offset + i) & MASK];
        }
        
        T& front() {
            return _array[_offset & MASK];
        }
        
    }; // struct InlineRingBuffer<T, N, MASK>
    
    

    
    
    struct Collector {
        
        InlineRingBuffer<Color, 4> _color_history;
        InlineRingBuffer<Color, 4> _shade_history;
        
        Bag<const GarbageCollected*> _known_objects;
        
        Color _color_for_allocation = 0;
        Color _color_in_use = 0;
        Color _mask_for_tracing = 0;
        Color _mask_for_deleting = 0;
        Color _mask_for_clearing = 0;
        
        Atomic<bool> _is_canceled;
        
        Stack<const GarbageCollected*> _greystack;
        
        
        
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
                    Color did_shade = {};
                    Report* head = collector_takes_reports();
                    while (head) {
                        did_shade |= head->color_did_shade;
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
                _color_history.push_front(_color_for_allocation);
                _global_atomic_color_for_allocation.store(_color_for_allocation, Ordering::RELAXED);

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
                        
            Color old_mask_for_deleting = _mask_for_deleting;
            Color old_mask_for_clearing = _mask_for_clearing;
            
            {
                // When all threads have acknowledged k-grey, publish k-black
                _color_for_allocation |= (_color_history[0] & ~_color_history[1]) << 32;
            }
            
            {
                // When all threads have acknowledged k-black, start tracing
                _mask_for_tracing |= (_color_history[0] & ~_color_history[1]) >> 32;
            }
            
            {
                // When we can prove all threads have made no new k-grey
                // during a whole sweep
                Color color_is_stable = _mask_for_tracing;
                color_is_stable &= ~_shade_history[0];
                color_is_stable &= ~_shade_history[1];
                color_is_stable &= ~_shade_history[2];
                // Stop tracing these colors
                _mask_for_tracing &= ~color_is_stable;
                // Start deleting these whites
                _mask_for_deleting = color_is_stable;
            }
            
            {
                // When we have deleted k-white, unpublish k-grey and k-black
                assert(is_subset_of(old_mask_for_deleting, _color_for_allocation));
                _color_for_allocation &= ~(old_mask_for_deleting | (old_mask_for_deleting << 32));
            }
            
            {
                // When all threads stop using k-grey and k-black, clear all k-bits
                _mask_for_clearing = (~_color_history[1] & _color_history[2]);
                // We need to wait two cycles so that the collector
                // has received objects allocated k-white by a leading
                // mutator but shaded grey by a trailing mutator
                // This means that we will clear objects in all k-states:
                // recently allocated white, old allocated black, and
                // recently allocated white and shaded black by leading and
                // trailling mutators
            }
            
            {
                _color_in_use &= ~old_mask_for_clearing;
                Color new_grey = (_color_in_use + 1) & ~_color_in_use & LOW_MASK;
                _color_for_allocation |= new_grey;
                _color_in_use |= new_grey;
                _color_in_use |= new_grey << 32;
            }
            
        } // void Collector::advance_state()
        
        
        void scan() {
            
#pragma mark Scan all known objects

            Bag<const GarbageCollected*> survivors;
            
            size_t trace_count = 0;
            size_t mark_count = 0;
            size_t delete_count = 0;
            // auto t0 = std::chrono::steady_clock::now();
            
            assert(_greystack.debug_is_empty());
            assert(survivors.debug_is_empty());
            assert(global_children.c.empty());
            
            // validate state:
            
            assert(is_subset_of(_color_for_allocation, _color_in_use));
            
            assert(is_subset_of(_mask_for_tracing, _color_in_use));
            assert(is_subset_of(_mask_for_deleting, _color_in_use));
            assert(is_subset_of(_mask_for_clearing, _color_in_use));
            
            assert(is_subset_of(_mask_for_tracing, _color_for_allocation));
            assert((_mask_for_tracing & _mask_for_deleting) == 0);
            assert((_mask_for_tracing & _mask_for_clearing) == 0);
            assert((_mask_for_deleting & _mask_for_clearing) == 0);
            assert((_mask_for_clearing & _color_for_allocation) == 0);
            
            // dump(old_color_for_allocation);
            //dump(_color_for_allocation);
            
            // dump(old_mask_for_tracing);
            //dump(_mask_for_tracing);
            
            // dump(old_mask_for_deleting);
            //dump(_mask_for_deleting);
            
            // dump(old_mask_for_clearing);
            //dump(_mask_for_clearing);
            
            //            printf("C0: Start scanning %zd objects with\n"
            //                   "              trace mask %016llx\n"
            //                   "             delete mask %016llx\n"
            //                   "              clear mask %016llx\n"
            //                   "    color_for_allocation %016llx\n",
            //                   _known_objects.debug_size(),
            //                   _mask_for_tracing,
            //                   _mask_for_deleting,
            //                   _mask_for_clearing,
            //                   _color_for_allocation);
            
            // While any objects are unprocessed
            for (;;) {
                
                // Depth-first recusively trace all the known children
                
                const GarbageCollected* parent = nullptr;
                while (_greystack.try_pop(parent)) {
                    assert(parent);
                    Color parent_color = parent->_color.load(Ordering::RELAXED);
                    parent->_garbage_collected_scan();
                    const GarbageCollected* child = nullptr;
                    while (global_children.try_pop(child)) {
                        Color after = 0;
                        Color before = child->_color.load(Ordering::RELAXED);
                        do  {
                            assert(is_subset_of(before, _color_in_use));
                            after = before | (parent_color & _mask_for_tracing);
                            Color mark = (after & _mask_for_tracing) << 32;
                            after = (after | mark) & ~_mask_for_clearing;
                            assert(is_subset_of(after, _color_in_use));
                        } while ((after != before) &&
                                 !child->_color.compare_exchange_weak(before,
                                                                      after,
                                                                      Ordering::RELAXED,
                                                                      Ordering::RELAXED));
                        Color did_set = ((~before) & after);
                        if (did_set) {
                            ++mark_count;
                            _greystack.push(child);
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
                // - k-mark: k-grey -> k-black, and enqueue for tracing
                // - k-clear: k-* -> k-white
                Color after = 0;
                Color before = object->_color.load(Ordering::RELAXED);
                for (;;) {
                    assert(is_subset_of(before, _color_in_use));
                    Color mark = (before & _mask_for_tracing) << 32;
                    after = (before | mark) & ~_mask_for_clearing;
                    assert(is_subset_of(after, _color_in_use));
                    if (after == before)
                        // No change to write
                        // TODO: Is this actually an optimization?
                        break;
                    if (object->_color.compare_exchange_weak(before,
                                                             after,
                                                             Ordering::RELAXED,
                                                             Ordering::RELAXED))
                        // Changed the color
                        break;
                    // Compare exchange failed, start over
                }
                Color did_set = (~before) & after;
                assert((did_set & LOW_MASK) == 0); // Never k-white -> k-grey
                bool must_trace = did_set & HIGH_MASK; // If k-grey -> k->black
                if (must_trace) {
                    ++trace_count;
                    _greystack.push(object);
                }
                bool is_not_grey = (((before >> 32) & _mask_for_deleting) == (before & _mask_for_deleting));
                if ((_mask_for_deleting == 0) || (before & _mask_for_deleting)) {
                    // k-reachable
                    if (!is_not_grey) {
                        dump(before);
                        dump(after);
                        dump(did_set);
                        dump(did_set & HIGH_MASK);
                        dump(before & _mask_for_deleting);
                        abort();
                    }
                    survivors.push(std::move(object));
                } else {
                    // k-unreachable
                    if (must_trace) {
                        dump(before);
                        dump(after);
                        dump(did_set);
                        dump(did_set & HIGH_MASK);
                        dump(before & _mask_for_deleting);
                        abort();
                    }
                    // Must not be k-grey; k-grey would imply not k-stable
                    assert(are_grey(before & (_mask_for_deleting | (_mask_for_deleting << 32))) == 0);
                    delete object;
                    ++delete_count;
                }
                
            } // loop until no objects
            
            assert(_greystack.debug_is_empty());
            assert(_known_objects.debug_is_empty());
            assert(global_children.c.empty());
            _known_objects = std::move(survivors);
            
            // auto t1 = std::chrono::steady_clock::now();
            //
            // printf("C0:     marked %zd\n", trace_count + mark_count);
            // printf("C0:     deleted %zd\n", delete_count);
            // printf("C0:     in %.3gs\n", std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
                        
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


