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
    
    struct Session;
    
    using namespace detail;
    
#pragma mark - Global and thread_local variables
    
    constinit static Atomic<Session*> _global_new_sessions;
    constinit static Atomic<Color> _global_atomic_color_for_allocation;
    
    constinit thread_local Color _thread_local_color_for_allocation;
    constinit thread_local Color _thread_local_color_did_shade;
    constinit thread_local Session* _thread_local_session;
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;
    
    Color get_global_color_for_allocation() {
        return _global_atomic_color_for_allocation.load(Ordering::RELAXED);
    }
    
    inline Color get_thread_local_color_for_allocation() {
        return _thread_local_color_for_allocation;
    }
    
    inline Color get_thread_local_color_for_shade() {
        return _thread_local_color_for_allocation & LOW_MASK;
    }
    
    // TODO: statistics
    //
    // thread_local pointer, to a heap allocated struct of counters, that is
    // itself part of a global intrusive linked list?
    static constinit Atomic<ptrdiff_t> total_deleted;
    
    // A session exists between a thread becoming a mutator, multiple handshakes,
    // and resiging mutator status.  Name it Mutator?
    
    struct Session {
        
        Session* _next = nullptr;
        
        struct Node {
            
            Node* _next = nullptr;
            Color color_did_shade = 0;
            Bag<const GarbageCollected*> allocations;
            
        }; // struct Node
        
        enum struct Tag : intptr_t {
            COLLECTOR_SHOULD_CONSUME = 0,
            MUTATOR_SHOULD_PUBLISH = 1,
            COLLECTOR_SHOULD_CONSUME_AND_RELEASE = 2,
            MUTATOR_SHOULD_PUBLISH_AND_NOTIFY = 3,
        };
        
        Atomic<TaggedPtr<Node, Tag>> _atomic_tagged_head{
            TaggedPtr<Node, Tag>{
                nullptr,
                Tag::COLLECTOR_SHOULD_CONSUME
            }
        };
        
        // Stuff the collector needs to track for each mutator
        struct State {
            bool is_done = false;
        };
        State collector_state;
        
        std::string _name;
        
        
        // We use reference counting to manage the Session lifetime
        Atomic<ptrdiff_t> _reference_count_minus_one{1};
        
        void acquire() {
            _reference_count_minus_one.fetch_add(1, Ordering::RELAXED);
        }
        
        void release() {
            if (!_reference_count_minus_one.fetch_sub(1, Ordering::RELEASE)) {
                _reference_count_minus_one.load(Ordering::ACQUIRE);
                printf("%s: garbage collection session ends\n", _name.c_str());
                delete this;
            }
        }
        
        
        
        
        void handshake() {
            
            TaggedPtr<Node, Tag> expected = _atomic_tagged_head.load(Ordering::RELAXED);
            switch (expected.tag) {
                    
                case Tag::COLLECTOR_SHOULD_CONSUME: {
                    // The collector doesn't need anything from us at this time
                } break;
                    
                case Tag::MUTATOR_SHOULD_PUBLISH:
                    [[fallthrough]];
                case Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY: {
                    // The collector needs us to publish what we have and
                    // refresh our color_for_allocation.  It may not have
                    // consumed our last report, so we always respect the
                    // pointer and maintain the chain
                    auto desired = TaggedPtr<Node, Tag>{
                        new Node{
                            expected.ptr,
                            std::exchange(_thread_local_color_did_shade, 0),
                            std::move(_thread_local_new_objects)
                        },
                        Tag::COLLECTOR_SHOULD_CONSUME
                    };
                    expected = _atomic_tagged_head.exchange(desired, Ordering::ACQ_REL);
                    if (expected.tag == Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY)
                        _atomic_tagged_head.notify_one();
                    _thread_local_color_for_allocation = get_global_color_for_allocation();
                } break;
                    
                case Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE: {
                    // We should not be handshaking after we have called release
                    abort();
                }
                    
            } // switch (expected.tag)
            
        } // void Session::handshake()
        
        
        void resign() {
            
            printf("%s: garbage collection session resigns\n", _name.c_str());
            
            TaggedPtr<Session::Node, Session::Tag> desired{
                new Session::Node{
                    nullptr,
                    std::exchange(_thread_local_color_did_shade, 0),
                    std::move(_thread_local_new_objects)
                },
                Session::Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE
            };
            
            TaggedPtr<Node, Tag> expected = _atomic_tagged_head.load(Ordering::RELAXED);
            for (;;) {
                
                switch (expected.tag) {
                        
                    case Tag::COLLECTOR_SHOULD_CONSUME:
                    case Tag::MUTATOR_SHOULD_PUBLISH:
                    case Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY:
                        // Permitted states
                        break;
                        
                    case Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE:
                        // We should not call release twice
                    default:
                        abort();
                        
                } // switch (expected.tag)
                
                desired->_next = expected.ptr;
                if (_atomic_tagged_head.compare_exchange_weak(expected,
                                                              desired,
                                                              Ordering::RELEASE,
                                                              Ordering::RELAXED)) {
                    
                    if (expected.tag == Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY)
                        _atomic_tagged_head.notify_one();
                    
                    return;
                }
                
            } // for (;;)
            
        } // void Session::resign()
        
        
        
        auto wait_for_report() -> TaggedPtr<Node, Tag> {
            TaggedPtr<Node, Tag> expected = _atomic_tagged_head.load(Ordering::RELAXED);
            TaggedPtr<Node, Tag> desired = {};
            for (;;) switch (expected.tag) {
                case Tag::COLLECTOR_SHOULD_CONSUME:
                case Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE:
                    desired = { nullptr, expected.tag };
                    return _atomic_tagged_head.exchange(desired,
                                                        Ordering::ACQUIRE);
                case Tag::MUTATOR_SHOULD_PUBLISH: {
                    desired = { nullptr, Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY };
                    if (!_atomic_tagged_head.compare_exchange_weak(expected,
                                                                   desired,
                                                                   Ordering::RELAXED,
                                                                   Ordering::RELAXED))
                        break;
                    expected = desired;
                } [[fallthrough]];
                case Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY:
                    _atomic_tagged_head.wait(expected, Ordering::RELAXED);
                    break;
            } // for (;;) switch (expected.tag)
        } // get_report
        
    }; // struct Session
    
    
    void record_child(void* tracer, const GarbageCollected* child) {
        assert(child);
        ((Stack<const GarbageCollected*>*)tracer)->push(child);
    }
    
    
    
    
    
    
    
    void mutator_become_with_name(const char* name) {
        
        Session* session = new Session{
            ._name = name
        };
        
        session->_next = _global_new_sessions.load(Ordering::RELAXED);
        while (!_global_new_sessions.compare_exchange_weak(session->_next, session, Ordering::RELEASE, Ordering::RELAXED))
            ;
        if (!session->_next)
            _global_new_sessions.notify_one();
        
        _thread_local_session = session;
        _thread_local_color_for_allocation = get_global_color_for_allocation();
        
        printf("%s: garbage collection session begins\n", name);

        
    }
    
    
    
    
    GarbageCollected::GarbageCollected()
    : _color(get_thread_local_color_for_allocation()) {
        // SAFETY: pointer to a partially constructed object escapes.  These
        // pointers are only published to the collector thread after the
        // constructor has completed.
        _thread_local_new_objects.push(this);
    }
    
    void GarbageCollected::_garbage_collected_shade() const {
        const Color color_for_shade = get_thread_local_color_for_shade();
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
        // if (child) {
        //     global_children.push(child);
        // }
    }
    
    
    struct Collector {
        
        template<typename T, size_t N, size_t MASK = N-1>
        struct RingBuffer {
            
            static_assert(std::has_single_bit(N), "RingBuffer capacity must be a power of two");
            
            size_t offset = 0;
            T _array[N] = {};
            
            void push_front(T value) {
                _array[--offset &= MASK] = value;
            }
            
            const T& operator[](ptrdiff_t i) const {
                assert((0 <= i) && (i < N));
                return _array[(offset + i) & MASK];
            }
            
        }; // struct RingBuffer<T, N, MASK>
        
        // std::vector<Session*> _sessions;
        Session* _sessions_head = nullptr;
        
        RingBuffer<Color, 4> _color_history;
        RingBuffer<Color, 4> _shade_history;
        
        Bag<const GarbageCollected*> _known_objects;
        
        Color _color_for_allocation = 0;
        Color _color_in_use = 0;
        Color _mask_for_tracing = 0;
        Color _mask_for_deleting = 0;
        Color _mask_for_clearing = 0;
        
        Atomic<bool> _is_canceled;
        
        Stack<const GarbageCollected*> _greystack;
        
        void loop_until_canceled() {
            
            // The collector also registers itself as a mutator:
            
            {
                Session* session = new Session{
                    ._name = "C0",
                };
                _thread_local_session = session;
                _thread_local_color_for_allocation = get_global_color_for_allocation();
                // _sessions.push_back(session);
                _sessions_head = session;
            }
            
            printf("C0: garbage collector starts\n");
            
            while (!_is_canceled.load(Ordering::RELAXED)) {
                
                // The collector at least knows about itself-as-mutator
                // assert(!_sessions.empty());
                assert(_sessions_head);
                
                if (_known_objects.debug_is_empty()) {
                    printf("C0: No known objects!\n");
                }
                
#pragma mark Receive all mutator messages
                
                // A thread report covers events in a given interval
                
                {
                    Color did_shade = 0;
                    Session** a = &_sessions_head;
                    // for (auto& u : _sessions) {
                    for (;;) {
                        Session* b = *a;
                        if (!b)
                            break;
                        auto& v = b->collector_state;
                        // TODO: This is blocking
                        TaggedPtr<Session::Node, Session::Tag> report = b->wait_for_report();
                        Session::Node* node = report.ptr;
                        bool should_release = false;
                        while (node) {
                            did_shade |= node->color_did_shade;
                            if (report.tag == Session::Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE)
                                should_release = true;
                            _known_objects.splice(std::move(node->allocations));
                            delete std::exchange(node, node->_next);
                        }
                        if (should_release)
                            std::exchange(*a, b->_next)->release();
                        else
                            a = &(b->_next);
                    }
                    // We only care about the combined shading history of all
                    // threads in the era
                    _shade_history.push_front(did_shade);
                }
                
#pragma mark Compute new state
                
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
                    _mask_for_tracing &= ~color_is_stable;
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
                
#pragma mark Publish the new color for allocation
                
                _color_history.push_front(_color_for_allocation);
                _global_atomic_color_for_allocation.store(_color_for_allocation, Ordering::RELAXED);
                
#pragma mark Accept new sessions
                
                {
                    Session* expected = nullptr;
                    if (!_sessions_head->_next && _known_objects.debug_is_empty()) {
                        _global_new_sessions.wait(expected, Ordering::RELAXED);
                        if (_is_canceled.load(Ordering::RELAXED))
                            break;
                    }
                    expected = _global_new_sessions.exchange(nullptr, Ordering::ACQUIRE);
                    while (expected) {
                        Session* a = expected;
                        expected = expected->_next;
                        a->_next = _sessions_head;
                        _sessions_head = a;
                    }
                }
                
#pragma mark Request mutator updates
                
                for (Session* p = _sessions_head; p; p = p->_next) {
                    auto expected = p->_atomic_tagged_head.load(Ordering::RELAXED);
                    // We don't care about any publications the mutator has made
                    // since we consumed them
                GAMMA:
                    switch (expected.tag) {
                        case Session::Tag::COLLECTOR_SHOULD_CONSUME: {
                            TaggedPtr<Session::Node, Session::Tag> desired{
                                expected.ptr,
                                Session::Tag::MUTATOR_SHOULD_PUBLISH
                            };
                            if (!p->_atomic_tagged_head.compare_exchange_weak(expected,
                                                                              desired,
                                                                              Ordering::RELEASE,
                                                                              Ordering::RELAXED)) {
                                goto GAMMA;
                            }
                        } break;
                        case Session::Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE:
                            // The resignation will be processed when we get the
                            // next report
                            break;
                        case Session::Tag::MUTATOR_SHOULD_PUBLISH:
                        case Session::Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY:
                        default:
                            // Not allowed
                            abort();
                    }
                    
                }
                
                _thread_local_session->handshake();
                
#pragma mark Receive new mutators
                
#pragma mark Visit every object to trace, garbage_collected_shade, sweep and clean
                
                scan();
                
            } // loop until killed
            
        } // Collector::loop
        
        
        
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
                
#pragma mark Depth-first recusively trace all children
                
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
                
#pragma mark Process each object
                
                const GarbageCollected* object = nullptr;
                if (!_known_objects.try_pop(object))
                    break;
                assert(object);
                // process the object
                Color after = 0;
                Color before = object->_color.load(Ordering::RELAXED);
                do {
                    assert(is_subset_of(before, _color_in_use));
                    Color mark = (before & _mask_for_tracing) << 32;
                    after = (before | mark) & ~_mask_for_clearing;
                    assert(is_subset_of(after, _color_in_use));
                } while ((after != before) &&
                         !object->_color.compare_exchange_weak(before,
                                                               after,
                                                               Ordering::RELAXED,
                                                               Ordering::RELAXED));
                Color did_set = (~before) & after;
                assert((did_set & LOW_MASK) == 0);
                bool must_trace = did_set & HIGH_MASK;
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
                    // must not be grey; grey would imply not k-stable
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
            
            total_deleted.fetch_add(delete_count, Ordering::RELAXED);
            
            
        } // void scan()
        
    }; // struct Collector
    
    static constinit Collector collector = {};
    
    void collector_run_on_this_thread() {
        collector.loop_until_canceled();
    }
    
    void collector_cancel() {
        collector._is_canceled.store(true, Ordering::RELAXED);
        _global_new_sessions.notify_all();
    }
    
    void mutator_handshake() {
        _thread_local_session->handshake();
    }
    
    void mutator_resign() {
        _thread_local_session->resign();
        std::exchange(_thread_local_session, nullptr)->release();
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


