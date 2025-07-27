//
//  gc.cpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#include <cinttypes>
#include <thread>
#include <queue>
#include <deque>

#include "gc.hpp"

#include "bag.hpp"
#include "channel.hpp"
//#include "ctrie.hpp"
#include "garbage_collected.hpp"
//#include "HeapArray.hpp"
#include "HeapString.hpp"
//#include "HeapTable.hpp"
#include "stack.hpp"
#include "tagged_ptr.hpp"
#include "utility.hpp"
#include "value.hpp"

#include "test.hpp"

#define dump(X) printf("C0.%d: %016llx = " #X "\n", __LINE__, (X));


namespace wry {
    
#pragma mark - Forward declarations
    
    struct Session;

#pragma mark - Global and thread_local variables

    // Globals are unfashionable but passing a context pointer to every
    // function is worse.  Think of the thread local storage register as an
    // implicit monadic argument if you wish.
    //
    // They are constinit to avoid the static initialization order fiasco
        
    constinit static Atomic<Session*> _global_new_sessions;
    constinit static Atomic<Color> _global_atomic_color_for_allocation;
    
    constinit thread_local Color _thread_local_color_for_allocation;
    constinit thread_local Color _thread_local_color_did_shade;
    constinit thread_local Session* _thread_local_session;
    constinit thread_local Bag<const GarbageCollected*> _thread_local_new_objects;
    // TODO: Bag destructor at thread exit?  Should be empty when there is no
    // Session in progress.
    
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
                printf("%s(interface): goodbye\n", _name.c_str());
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
                        
    }; // struct Session
    

    using Tracer = Stack<const GarbageCollected*>;
    
    void record_child(void* tracer, const GarbageCollected* child) {
        assert(child);
        ((Tracer*)tracer)->push(child);
    }
        
    
    


    
    
    void mutator_become_with_name(const char* name) {

        Session* session = new Session{
            ._name = name
        };
        
        session->_next = _global_new_sessions.load(Ordering::RELAXED);
        while (!_global_new_sessions.compare_exchange_weak(session->_next, session, Ordering::RELEASE, Ordering::RELAXED))
            ;
        _global_new_sessions.notify_one();
                
        _thread_local_session = session;
        _thread_local_color_for_allocation = get_global_color_for_allocation();
                
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
    
    void GarbageCollected::_garbage_collected_trace(void* tracer) const {
        collector_acknowledge_child(tracer, this);
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
        
        std::vector<Session*> _known_mutator_interfaces;
        
        RingBuffer<Color, 4> _color_history;
        RingBuffer<Color, 4> _shade_history;
        
        Bag<const GarbageCollected*> _known_objects;
        
        Color _color_for_allocation = 0;
        Color _color_in_use = 0;
        Color _mask_for_tracing = 0;
        Color _mask_for_deleting = 0;
        Color _mask_for_clearing = 0;
        
        void loop_until(std::chrono::steady_clock::time_point collector_deadline) {
            
            // The collector also registers itself as a mutator:
            
            {
                Session* session = new Session{
                    ._name = "C0",
                };
                _thread_local_session = session;
                _thread_local_color_for_allocation = get_global_color_for_allocation();
                _known_mutator_interfaces.push_back(session);
            }
            
            printf("C0: go\n");
            
            // HACK: loop until a time significantly later than the mutator stop time
            while (std::chrono::steady_clock::now() < collector_deadline) {
                
                // The collector at least knows about itself-as-mutator
                assert(!_known_mutator_interfaces.empty());
                
                if (_known_objects.debug_is_empty()) {
                    printf("C0: No known objects!\n");
                }
                
#pragma mark Receive all mutator messages
                
                // A thread report covers events in a given interval
                
                {
                    //printf("C0: There are %zd known mutators\n", _known_mutator_interfaces.size());
                    size_t number_of_new_objects = 0;
                    size_t number_of_resignations = 0;
                    Color did_shade = 0;
                    
                    // TODO: keep the Sessions in a linked list?
                    // Delete them on the fly?
                    for (auto& u : _known_mutator_interfaces) {
                        auto& v = u->collector_state;
                        //printf("C0: try_pop \"%s\"\n", u->_name.c_str());
                        TaggedPtr<Session::Node, Session::Tag> expected = {};
                        expected = u->_atomic_tagged_head.load(Ordering::RELAXED);
                    SWITCH_ON_EXPECTED_TAG:
                        switch (expected.tag) {
                            case Session::Tag::COLLECTOR_SHOULD_CONSUME:
                            case Session::Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE:
                                expected = u->_atomic_tagged_head.exchange(TaggedPtr<Session::Node, Session::Tag>{
                                    nullptr,
                                    expected.tag
                                },
                                                                Ordering::ACQUIRE);
                                break;
                            case Session::Tag::MUTATOR_SHOULD_PUBLISH: {
                                TaggedPtr<Session::Node, Session::Tag> desired{
                                    nullptr,
                                    Session::Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY
                                };
                                if (u->_atomic_tagged_head.compare_exchange_weak(expected,
                                                                                 desired,
                                                                                 Ordering::RELAXED,
                                                                                 Ordering::RELAXED)) {
                                    expected = desired;
                                } else {
                                    goto SWITCH_ON_EXPECTED_TAG;
                                }
                            } [[fallthrough]];
                            case Session::Tag::MUTATOR_SHOULD_PUBLISH_AND_NOTIFY:
                                // TODO: timeout ?
                                u->_atomic_tagged_head.wait(expected, Ordering::RELAXED);
                                goto SWITCH_ON_EXPECTED_TAG;
                        } // switch (expected.tag)
                        Session::Node* node = expected.ptr;
                        while (node) {
                            did_shade |= node->color_did_shade;
                            v.is_done = v.is_done || (expected.tag == Session::Tag::COLLECTOR_SHOULD_CONSUME_AND_RELEASE);
                            number_of_new_objects += node->allocations.debug_size();
                            _known_objects.splice(std::move(node->allocations));
                            delete std::exchange(node, node->_next);
                        }
                        if (v.is_done) {
                            ++number_of_resignations;
                        }
                    }
                    // We only care about the combined shading history of all
                    // threads in the era
                    _shade_history.push_front(did_shade);
                    
                    //printf("C0: ack %zd resignations\n", number_of_resignations);
                    //printf("C0: ack %zd new objects\n", number_of_new_objects);
                }
                
#pragma mark Process resignations from mutator status
                                
                std::erase_if(_known_mutator_interfaces, [](Session* x) -> bool {
                    bool is_done = x->collector_state.is_done;
                    if (is_done) {
                        printf("C0: forgetting a mutator\n");
                        x->release();
                    }
                    return is_done;
                });
                                
#pragma mark Compute new state
                
                //Color old_color_for_allocation = _color_for_allocation;
                //Color old_mask_for_tracing = _mask_for_tracing;
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
                
#pragma mark Publish the color for allocation and timestamp
                
                _color_history.push_front(_color_for_allocation);
                _global_atomic_color_for_allocation.store(_color_for_allocation, Ordering::RELAXED);
                
                {
                    Session* expected = nullptr;
                    if ((_known_mutator_interfaces.size() == 1) && _known_objects.debug_is_empty()) {
                        printf("C0: Waiting for work\n");
                        // _new_mutator_interfaces.hack_wait_until(collector_deadline);
                        _global_new_sessions.wait(expected, Ordering::RELAXED);
                        printf("C0: Woke\n");
                    }
                    //while (_new_mutator_interfaces.try_pop(victim)) {
                    //_known_mutator_interfaces.push_back(std::move(victim));
                    //printf("C0: A mutator enrolled\n");
                    //}
                    expected = _global_new_sessions.exchange(nullptr, Ordering::ACQUIRE);
                    while (expected) {
                        _known_mutator_interfaces.push_back(expected);
                        //printf("C0: A mutator enrolled\n");
                        expected = expected->_next;
                    }
                }
                
                for (Session* p : _known_mutator_interfaces) {
                    //MessageFromCollectorToMutator incoming = {
                        // .color_for_allocation = _color_for_allocation,
                    //};
                    // p->_channel_from_collector_to_mutator.push(std::move(incoming));
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
                            // The thread is done, leave it alone
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
                
#pragma mark Visit every object to trace, shade, sweep and clean
                
                scan();
                
            } // loop until killed
            
        } // Collector::loop
        
        void scan() {
            
#pragma mark Scan all known objects
            
            Stack<const GarbageCollected*> greystack;
            Stack<const GarbageCollected*> children;
            Bag<const GarbageCollected*> survivors;
            
            size_t trace_count = 0;
            size_t mark_count = 0;
            size_t delete_count = 0;
            auto t0 = std::chrono::steady_clock::now();
            
            assert(greystack.c.empty());
            assert(survivors.debug_is_empty());
            assert(children.c.empty());
            
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
                while (greystack.try_pop(parent)) {
                    assert(parent);
                    Color parent_color = parent->_color.load(Ordering::RELAXED);
                    parent->_garbage_collected_enumerate_fields((GarbageCollected::TraceContext*)&children);
                    const GarbageCollected* child = nullptr;
                    while (children.try_pop(child)) {
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
                            greystack.push(child);
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
                    greystack.push(object);
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
            
            assert(greystack.c.empty());
            assert(_known_objects.debug_is_empty());
            assert(children.c.empty());
            _known_objects = std::move(survivors);
            
            //auto t1 = std::chrono::steady_clock::now();
            //
            //printf("C0:     marked %zd\n", trace_count + mark_count);
            //printf("C0:     deleted %zd\n", delete_count);
            //printf("C0:     in %.3gs\n", std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
            
            total_deleted.fetch_add(delete_count, Ordering::RELAXED);
            
            
        }
        
    };
    
    
    
    // TODO: make constinit
    static Collector collector;
    
    void collector_run_on_this_thread_until(std::chrono::steady_clock::time_point collector_deadline) {
        collector.loop_until(collector_deadline);
    }
    
    void mutator_handshake() {
        _thread_local_session->handshake();
    }
    
    void mutator_resign() {
        _thread_local_session->resign();
        std::exchange(_thread_local_session, nullptr)->release();
    }
    
    void collector_acknowledge_child(void* tracer, const GarbageCollected* child) {
        record_child(tracer, child);
    }
    
    void mutator_did_overwrite(const GarbageCollected* a) {
        if (a) {
            a->_garbage_collected_shade();
        }
    }
    
    

    
}







// LEGACY

namespace wry {
    
    const HeapString* HeapString::make(size_t hash, string_view view) {
        abort();
#if 0
        return global_collector->string_ctrie->find_or_emplace(_ctrie::Query{hash, view});
#endif
    }
    
    Color HeapString::_garbage_collected_sweep() const {
        abort();
#if 0
        // Try to condemn the string to the terminal RED state
        Color expected = Color::WHITE;
        if (color.compare_exchange(expected, Color::RED)) {
            global_collector->string_ctrie->erase(this);
            return Color::RED;
        } else {
            return expected;
        }
#endif
    }
    
}


#if 0

namespace wry {
    
    // Bag is unordered storage optimized to make the mutator's common
    // operations cheap
    //
    // True O(1) push to append to log
    // True O(1) splice to combine logs
    
    // Tracks tail to permit splicing
    // Tracks count to permit empty and statistics
    

        
    // Log of a Mutator's actions since the last handshake with the Collector
    
    struct Log {
        
        bool dirty;
        Bag<const GarbageCollected*> allocations;
        std::intptr_t bytes_allocated;
        std::intptr_t bytes_deallocated;

        Log();
        Log(const Log&) = delete;
        Log(Log&& other);
        ~Log();
        
        Log& operator=(const Log&) = delete;
        Log& operator=(Log&&) = delete;
        
        Log& splice(Log&& other);
        
    }; // struct Log
    
    struct LogNode : Log {

        LogNode* log_list_next = nullptr;
        
        explicit LogNode(Log&&);
        
        
    }; // struct LogNode
    
    
    struct Channel {
        
        enum class Tag : std::intptr_t {
            NOTHING,
            COLLECTOR_DID_REQUEST_HANDSHAKE,
            COLLECTOR_DID_REQUEST_WAKEUP,
            COLLECTOR_DID_REQUEST_MUTATOR_LEAVES,
            MUTATOR_DID_PUBLISH_LOGS,
            MUTATOR_DID_LEAVE,
            MUTATOR_DID_REQUEST_COLLECTOR_STOPS,
        };

        Atomic<intptr_t> reference_count{2};
        Channel* entrant_list_next = nullptr;
        Atomic<TaggedPtr<LogNode, Channel::Tag>> log_stack_head;

        Channel();
        void release();
        
    };
    
    // garbage collector state for one mutator thread
    
    struct Mutator {
        
        Channel* channel = nullptr;
        Log mutator_log;

        void publish_log_with_tag(Channel::Tag tag);

        void handshake();

        void enter();
        void leave();
        
    };
        
    // garbage collector state for the unique collector thread, which is
    // also a mutator
    
    struct Collector : Mutator {

        // These variables are loaded by all threads very frequently (per shade
        // and new), and only stored to by the collector infrequently (per
        // round of handshakes).  It should be beneficial to put them on their
        // own cache line so they are not frequently invalidated by writes to
        // hot fields of the collector such as .gray_stack.__end_.
        
        struct alignas(CACHE_LINE_SIZE) {
            Atomic<std::underlying_type_t<Color>> atomic_encoded_color_encoding;
            Atomic<std::underlying_type_t<Color>> atomic_encoded_color_alloc;
            Ctrie* string_ctrie = nullptr;
        };
        
        Atomic<Channel*> entrant_list_head;
        std::vector<Channel*> active_channels;
        Log collector_log;
        Bag<const GarbageCollected*> object_bag;
        Bag<const GarbageCollected*> white_bag;
        Bag<const GarbageCollected*> black_bag;
        std::vector<const GarbageCollected*> gray_stack;
        Bag<const GarbageCollected*> red_bag;
        bool stop_requested = false;

        void collect();
        
        void set_alloc_to_black();
        void flip_encoded_color_encoding();

        void consume_log_list(LogNode* log_list_head);

        void initiate_handshakes();
        void finalize_handshakes();
        
        void synchronize_with_mutators();
        
                
    }; // struct Collector
    
    
    
    
    thread_local Mutator* thread_local_mutator = nullptr;
    
    Collector* global_collector = nullptr;
    

    
    AtomicEncodedColor::AtomicEncodedColor()
    : _encoded(global_collector->atomic_encoded_color_alloc.load(Ordering::RELAXED)) {
    }
    
    Color AtomicEncodedColor::load() const {
        std::underlying_type_t<Color> encoding = global_collector->atomic_encoded_color_encoding.load(Ordering::RELAXED);
        std::underlying_type_t<Color> encoded_discovered = _encoded.load(Ordering::RELAXED);
        return Color{encoded_discovered ^ encoding};
    }
    
    bool AtomicEncodedColor::compare_exchange(Color &expected, Color desired) {
        std::underlying_type_t<Color> encoding = global_collector->atomic_encoded_color_encoding.load(Ordering::RELAXED);
        std::underlying_type_t<Color> encoded_expected = std::to_underlying(expected) ^ encoding;
        std::underlying_type_t<Color> encoded_desired = std::to_underlying(desired) ^ encoding;
        bool result = _encoded.compare_exchange_strong(encoded_expected,
                                                       encoded_desired,
                                                       Ordering::RELAXED,
                                                       Ordering::RELAXED);
        expected = Color{encoded_expected ^ encoding};
        return result;
    }

    
    
    void* GarbageCollected::operator new(size_t count) {
        void* ptr = calloc(count, 1);
        thread_local_mutator->mutator_log.bytes_allocated += count;
        return ptr;
    }

    void GarbageCollected::operator delete(void* ptr) {
        free(ptr);
    }

    GarbageCollected::GarbageCollected()
    : color() {
        thread_local_mutator->mutator_log.allocations.push(this);
    }
    
    void GarbageCollected::_garbage_collected_shade() const {
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::GRAY);
        switch (expected) {
            case Color::WHITE:
                thread_local_mutator->mutator_log.dirty = true;
            case Color::BLACK:
            case Color::GRAY:
                break;
            case Color::RED:
            default:
                _garbage_collected_debug();
                abort();
                break;
        }
    }

    void GarbageCollected::_garbage_collected_trace() const {
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::BLACK);
        switch (expected) {
            case Color::WHITE:
                global_collector->gray_stack.push_back(this);
                break;
            case Color::BLACK:
            case Color::GRAY:
                break;
            case Color::RED:
            default:
                _garbage_collected_debug();
                abort();
                break;
        }
    }
    

    
    

    
    
    Log::Log()
    : dirty(false)
    , allocations()
    , bytes_allocated(0)
    , bytes_deallocated(0) {
    }
    
    Log::Log(Log&& other)
    : dirty(std::exchange(other.dirty, false))
    , allocations(std::move(other.allocations))
    , bytes_allocated(std::exchange(other.bytes_allocated, 0))
    , bytes_deallocated(std::exchange(other.bytes_deallocated, 0)) {
        assert(other.allocations.empty());
    }
    
    Log::~Log() {
        assert(!dirty);
        assert(allocations.empty());
        assert(bytes_allocated == 0);
        assert(bytes_deallocated == 0);
    }
    
    Log& Log::splice(Log&& other) {
        dirty = std::exchange(other.dirty, false) || dirty;
        allocations.splice(std::move(other.allocations));
        bytes_allocated += std::exchange(other.bytes_allocated, 0);
        bytes_deallocated += std::exchange(other.bytes_deallocated, 0);
        return *this;
    }
    
    
    
    LogNode::LogNode(Log&& other)
    : Log(std::move(other))
    , log_list_next(nullptr) {
    }
    
    
    
    Channel::Channel()
    : reference_count(2)
    , entrant_list_next(nullptr)
    , log_stack_head(TaggedPtr((LogNode*)nullptr,
                               Channel::Tag::NOTHING)) {
    }
    
    void Channel::release() {
        if (reference_count.sub_fetch(1, Ordering::RELEASE) == 0) {
            (void) reference_count.load(Ordering::ACQUIRE);
            delete this;
        }
    }
    
    

    void Mutator::publish_log_with_tag(Channel::Tag tag) {
        assert(thread_local_mutator == this);
        assert(channel);
        LogNode* node = new LogNode(std::move(this->mutator_log));
        assert(this->mutator_log.dirty == false);
        TaggedPtr desired(node, tag);
        TaggedPtr expected(channel->log_stack_head.load(Ordering::ACQUIRE));
        for (;;) {
            node->log_list_next = expected.ptr;
            if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                desired,
                                                                Ordering::RELEASE,
                                                                Ordering::ACQUIRE))
                break;
        }
        switch (expected.tag) {
           case Channel::Tag::COLLECTOR_DID_REQUEST_WAKEUP:
                channel->log_stack_head.notify_one();
                break;
            case Channel::Tag::NOTHING:
            case Channel::Tag::COLLECTOR_DID_REQUEST_HANDSHAKE:
            case Channel::Tag::COLLECTOR_DID_REQUEST_MUTATOR_LEAVES:
            case Channel::Tag::MUTATOR_DID_PUBLISH_LOGS:
            case Channel::Tag::MUTATOR_DID_LEAVE:
            case Channel::Tag::MUTATOR_DID_REQUEST_COLLECTOR_STOPS:
                break;
        }
    }
    
    void Mutator::handshake() {
        TaggedPtr expected(channel->log_stack_head.load(Ordering::ACQUIRE));
        switch (expected.tag) {
            case Channel::Tag::NOTHING:
                return;
            case Channel::Tag::COLLECTOR_DID_REQUEST_HANDSHAKE:
            case Channel::Tag::COLLECTOR_DID_REQUEST_WAKEUP:
                publish_log_with_tag(Channel::Tag::MUTATOR_DID_PUBLISH_LOGS);
                break;
            case Channel::Tag::COLLECTOR_DID_REQUEST_MUTATOR_LEAVES:
                leave();
                return;
            case Channel::Tag::MUTATOR_DID_PUBLISH_LOGS:
                return;
            case Channel::Tag::MUTATOR_DID_LEAVE:
            case Channel::Tag::MUTATOR_DID_REQUEST_COLLECTOR_STOPS:
            default:
                abort();
        }
    }
    
    void Mutator::enter() {
        assert(thread_local_mutator == this);
        assert(channel == nullptr);
        channel = new Channel;
        Atomic<Channel*>& head = global_collector->entrant_list_head;
        Channel*& next = channel->entrant_list_next;
        next = head.load(Ordering::ACQUIRE);
        while (!head.compare_exchange_strong(next,
                                             channel,
                                             Ordering::RELEASE,
                                             Ordering::ACQUIRE))
            ;
    }
    
    void Mutator::leave() {
        publish_log_with_tag(Channel::Tag::MUTATOR_DID_LEAVE);
        exchange(channel, nullptr)->release();
    }
    
    
    
    void Collector::flip_encoded_color_encoding() {
        std::underlying_type_t<Color> encoding = atomic_encoded_color_encoding.load(Ordering::RELAXED);
        atomic_encoded_color_encoding.store(encoding ^ 1, Ordering::RELAXED);
    }
    
    void Collector::set_alloc_to_black() {
        std::underlying_type_t<Color> encoding = atomic_encoded_color_encoding.load(Ordering::RELAXED);
        std::underlying_type_t<Color> encoded_black = std::to_underlying(Color::BLACK) ^ encoding;
        atomic_encoded_color_alloc.store(encoded_black, Ordering::RELAXED);
    }
    
    void Collector::consume_log_list(LogNode* log_list_head) {
        while (log_list_head) {
            auto a = log_list_head->log_list_next;
            assert(log_list_head != log_list_head->log_list_next);
            // TODO: we are moving from LogNode then using its next value
            // on the rationale that the move affects only the base Log class
            collector_log.splice(std::move(*(Log*)log_list_head));
            assert(log_list_head->log_list_next == a);
            delete exchange(log_list_head, log_list_head->log_list_next);
        }
    }
    
    void Collector::initiate_handshakes() {
        auto first = active_channels.begin();
        auto last = active_channels.end();
        while (first != last) {
            Channel* channel = *first;
            assert(channel);
            TaggedPtr desired((LogNode*)nullptr, Channel::Tag::COLLECTOR_DID_REQUEST_HANDSHAKE);
            TaggedPtr discovered = channel->log_stack_head.exchange(desired, Ordering::ACQ_REL);
            switch (discovered.tag) {
                case Channel::Tag::NOTHING: {
                    ++first;
                    break;
                }
                case Channel::Tag::MUTATOR_DID_LEAVE: {
                    LogNode* log_list_head = discovered.ptr;
                    assert(log_list_head);
                    consume_log_list(log_list_head);
                    exchange(channel, nullptr)->release();
                    --last;
                    if (first != last)
                        std::swap(*first, *last);
                    break;
                }
                default: {
                    abort();
                }
            } // switch (discovered.tag)
        } // while (first != last)
        active_channels.erase(last, active_channels.end());
    }
    
    void Collector::finalize_handshakes() {
        auto first = this->active_channels.begin();
        auto last = this->active_channels.end();
        while (first != last) {
            Channel* channel = *first;
            assert(channel);
            TaggedPtr<LogNode, Channel::Tag> expected;
            expected = channel->log_stack_head.load(Ordering::ACQUIRE);
            switch (expected.tag) {
                case Channel::Tag::COLLECTOR_DID_REQUEST_HANDSHAKE: {
                    // Attempt to set the wakeup flag
                    TaggedPtr desired((LogNode*)nullptr, Channel::Tag::COLLECTOR_DID_REQUEST_WAKEUP);
                    (void) channel->log_stack_head.compare_exchange_strong(expected,
                                                                           desired,
                                                                           Ordering::RELAXED,
                                                                           Ordering::ACQUIRE);
                    break;
                }
                case Channel::Tag::COLLECTOR_DID_REQUEST_WAKEUP:
                    switch (channel->log_stack_head.wait_for(expected,
                                                             Ordering::ACQUIRE,
                                                             1000000000)) {
                        case AtomicWaitResult::NO_TIMEOUT:
                            break;
                        case AtomicWaitResult::TIMEOUT:
                            fprintf(stderr, "Mutator unresponsive (1s)\n");
                            break;
                        default:
                            abort();
                    }
                    break;
                case Channel::Tag::MUTATOR_DID_PUBLISH_LOGS: {
                    LogNode* log_list_head = expected.ptr;
                    consume_log_list(log_list_head);
                    TaggedPtr desired((LogNode*)nullptr, Channel::Tag::NOTHING);
                    if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                        desired,
                                                                        Ordering::RELAXED,
                                                                        Ordering::ACQUIRE))
                        ++first;
                    break;
                }
                case Channel::Tag::MUTATOR_DID_LEAVE: {
                    LogNode* log_list_head = expected.ptr;
                    consume_log_list(log_list_head);
                    channel->release();
                    --last;
                    if (first != last)
                        std::swap(*first, *last);
                    break;
                }
                case Channel::Tag::MUTATOR_DID_REQUEST_COLLECTOR_STOPS: {
                    this->stop_requested = true;
                    LogNode* log_list_head = expected.ptr;
                    consume_log_list(log_list_head);
                    channel->release();
                    --last;
                    if (first != last)
                        std::swap(*first, *last);
                    break;
                }
                default: {
                    abort();
                }
            } // switch(expected.tag)
        } // while (first != last)
        active_channels.erase(last, active_channels.end());
    }
    
    void Collector::synchronize_with_mutators() {
        // Acquire entering mutators and release any changes to the color
        // encoding or alloc color
        Channel* head = entrant_list_head.exchange(nullptr, Ordering::ACQ_REL);
        
        // All entrants after this point will use the released colors
        while (head) {
            active_channels.push_back(head);
            head = head->entrant_list_next;
            // No processing of new entrants; they will have nothing to log,
            // unless they have already left too, which is handled below
        }
        
        // Use the channels to request that each mutator synchronizes with us
        // at its convenience
        initiate_handshakes();
        
        // Handshake ourself and shade our own root GarbageCollected objects
        this->handshake();
        shade(string_ctrie);
        
        // Wait for every mutator to handshake or leave
        finalize_handshakes();
        
    }
    
    void Collector::collect() {
        
        Mutator::enter();
                
        for (;;) {
            
            assert(black_bag.empty());
            assert(white_bag.empty());
            assert(gray_stack.empty());
            assert(red_bag.empty());
            
            // All mutators are allocating WHITE
            // The write barrier is shading WHITE objects GRAY
            
            // Change alloc color from WHITE to BLACK
            
            set_alloc_to_black();
            synchronize_with_mutators();
            object_bag.splice(std::move(collector_log.allocations));
            collector_log.dirty = false;
            
            // All objects allocated since the handshake will be BLACK and are
            // thus guaranteed to survive this cycle.
            
            // All mutators are allocating BLACK
            // The write barrier is turning WHITE objects GRAY (or BLACK)
            // All colors are present
            //
            // Scan for GRAY objects, shade their fields, and turn them BLACK
            
            for (;;) {
                
                while (!object_bag.empty()) {
                    const GarbageCollected* object = object_bag.top();
                    object_bag.pop();
                    assert(object);
                    Color expected = Color::GRAY;
                    object->color.compare_exchange(expected, Color::BLACK);
                    switch (expected) {
                        case Color::WHITE:
                            // Object is WHITE (but may turn GRAY at any time)
                            white_bag.push(object);
                            break;
                        case Color::GRAY:
                            // Was GRAY and is now BLACK
                            // Scan its fields to restore the invariant
                            object->_garbage_collected_scan();
                            [[fallthrough]];
                        case Color::BLACK:
                            // Is BLACK and will remain so
                            black_bag.push(object);
                            break;
                        case Color::RED:
                        default:
                            // "Impossible"
                            debug(object);
                            abort();
                    }
                    while (!gray_stack.empty()) {
                        // Depth first tracing
                        const GarbageCollected* object = gray_stack.back();
                        gray_stack.pop_back();
                        assert(object);
                        object->_garbage_collected_scan();
                    }
                }
                
                // Note that some of the objects we put in the white bag
                // may have been turned GRAY or BLACK by a mutator, or BLACK by
                // us when traced via a later object
                
                assert(object_bag.empty());
                object_bag.swap(white_bag);
                
                synchronize_with_mutators();
                if (!exchange(collector_log.dirty, false))
                    break;
                
                // At least one of the mutators reports that it has made a GRAY
                // object during the round, so we have to scan all the objects
                // we saw as WHITE again.  All new objects are BLACK, and all
                // traced objects are BLACK, and all roots are shaded every
                // round, so the white_bag will rapidly shrink to the reachable
                // set.  Within two rounds?
                
                // There is no need to rescan BLACK objects, they will remain
                // BLACK until
            }
            
            // All mutators are allocating BLACK
            // All mutators are clean
            // There are no GRAY objects
            //
            // Delete all WHITE objects
            
            
            // Sweep
            while (!object_bag.empty()) {
                const GarbageCollected* object = object_bag.top();
                object_bag.pop();                
                switch (object->_garbage_collected_sweep()) {
                    case Color::WHITE:
                        delete object;
                        break;
                    case Color::BLACK:
                        black_bag.push(object);
                        break;
                    case Color::RED:
                        red_bag.push(object);
                        break;
                    case Color::GRAY:
                    default:
                        debug(object);
                        abort();
                }
            }
            
            object_bag.swap(black_bag);
            
            // All objects are BLACK or RED
            // All mutators are allocating BLACK
            // There are no WHITE or GRAY objects
            // Mutators may be dereferencing RED objects
            
            // Redefine WHITE as BLACK
            
            flip_encoded_color_encoding();
            synchronize_with_mutators();
            
            // All mutators are allocating WHITE
            // Write barrier turns WHITE objects GRAY or BLACK
            // Mutators cannot discover RED objects
            
            // Delete all RED objects
            
            while (!red_bag.empty()) {
                const GarbageCollected* object = red_bag.top();
                red_bag.pop();
                delete object;
            }
            
            // All mutators are allocating WHITE
            // Write barrier turns WHITE objects GRAY or BLACK
            // There are no RED objects
            assert(red_bag.empty());
            
        } // for(;;)
        
    } // void Collector::collect()
    
    
    
    void mutator_enter() {
        if (!thread_local_mutator)
            thread_local_mutator = new Mutator;
        thread_local_mutator->enter();
    }
    
    void mutator_handshake() {
        thread_local_mutator->handshake();
    }
    
    void mutator_leave() {
        thread_local_mutator->leave();
    }

    // todo: move these into the Collector object?
    std::thread _collector_thread;
    Atomic<bool> _collector_done;
    
    void collector_start() {
        assert(global_collector == nullptr);
        global_collector = new Collector;
        thread_local_mutator = global_collector;
        thread_local_mutator->enter();
        global_collector->string_ctrie = new Ctrie;
        thread_local_mutator->leave();
        thread_local_mutator = nullptr;
        _collector_thread = std::thread([](){
            assert(!thread_local_mutator);
            thread_local_mutator = global_collector;
            global_collector->collect();
        });
    }
    
    void collector_stop() {
        _Exit(EXIT_SUCCESS);
        
        // The collector will be waiting on various mutators, including itself
        // and the main thread
        
        // We will be posting stop from a mutator thread, in particular the
        // main thread.

        // So the best way to stop the collector is to use the mutator to post
        // that status
        
        // If we want to start-stop-start the collector then we have to
        // keep the global collector state around to run on cumulatively
        
        // If we want to do parallel mark or sweep we need to make the collector
        // objects a bit more thread safe
        
        // _collector_done.store(true, Ordering::RELAXED);
        // _collector_thread.join();
    }
    
    bool collector_this_thread_is_collector_thread() {
        return thread_local_mutator == global_collector;
    }
    
    
    
    define_test("gc") {
        std::thread([](){
            assert(!thread_local_mutator);
            mutator_enter();
            for (int i = 0; i != 100; ++i) {
                auto p = new HeapInt64(787);
                
                foo();

                mutator_handshake();
                shade(p);
            }
            mutator_leave();
            delete exchange(thread_local_mutator, nullptr);
        }).detach();
    };
            
} // namespace wry




// Bags are what the mutator stores its recent allocations in.  They must
// have
// - Bounded worst case and cheap amortized push
//
// This is accomplished by a singly-linked list of fixed-size stacks.
//
// To merge bags, we can either track the tail node of the bag, or we
// can provide a second pointer to make a tree structure.  Tail seems less
// janky.  This makes atomicity harder, but do we ever need it on this object?

// An even more powerful structure would be a doubly linked list of
// circular buffers, which can operate as a deque.

template<typename T>
struct UnrolledLinkedList {
    
    struct Node {
        static constexpr ptrdiff_t CAPACITY = (1 << 8);
        static constexpr ptrdiff_t MASK = CAPACITY - 1;
        Node* _prev = nullptr;
        Node* _next = nullptr;
        ptrdiff_t _begin = 0;
        ptrdiff_t _end = 0;
        T _data[CAPACITY];
        bool try_push_back(T&& value) {
            bool result = (_end - _begin != CAPACITY);
            if (result) {
                _data[(_end++) & MASK] = std::move(value);
            }
            return result;
        }
        bool try_pop_front(T& victim) {
            bool result = _begin != _end;
            if (result) {
                victim = std::move(_data[(_begin++) & MASK]);
            }
            return result;
        }
    };
    
    Node* _head;
    Node* _tail;
    
    void push_back(T&& value) {
        while (!_tail || !_tail->try_push_back(std::move(value))) {
            Node* a = new Node{_tail, nullptr, 0, 0};
            if (_tail) {
                _tail->_next = a;
            } else {
                _head = a;
            }
            _tail = a;
        }
    }
    
    bool try_pop_front(T& victim) {
        for (;;) {
            if (!_head)
                return false;
            if (_head->try_pop_front(victim))
                return true;
            Node* a = _head;
            _head = a->_next;
            if (_head) {
                _head->_prev = nullptr;
            } else {
                _tail = nullptr;
            }
            delete a;
        }
    }
    
    void splice(UnrolledLinkedList&& other) {
        if (other._head) {
            if (_tail) {
                _tail->_next = other._head;
                other._head->_prev = _tail;
            } else {
                _head = other._head;
            }
            _tail = other._tail;
            other._head = nullptr;
            other._tail = nullptr;
        }
    }
};



// TODO: do we want to put allocation statistics in LogNode?  We could just
// do them thread locally and lazily combine them whenever.
// Allocate a statistics node at thread startup and link it into a global
// list, then on thread shutdown mark it for later deletion by whatever
// cares about crawling the statistics.  This is similar to the
// MutatorInterface but less transitory

// TODO: Make the MutatorInterface a lock-free linked list
// TODO: Cleanup names




#endif
