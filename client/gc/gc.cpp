//
//  gc.cpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#include <cinttypes>

#include <thread>

#include "bag.hpp"
#include "ctrie.hpp"
#include "gc.hpp"
#include "RealTimeGarbageCollectedDynamicArray.hpp"
#include "HeapTable.hpp"
#include "tagged_ptr.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "HeapString.hpp"

#include "test.hpp"

namespace wry::gc {
        
    // Log of a Mutator's actions since the last handshake with the Collector
    
    struct Log {
        
        bool dirty;
        Bag<const Object*> allocations;
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
            Atomic<int> atomic_encoded_color_encoding;
            Atomic<int> atomic_encoded_color_alloc;
            Ctrie* string_ctrie = nullptr;
        };
        
        Atomic<Channel*> entrant_list_head;
        std::vector<Channel*> active_channels;
        Log collector_log;
        Bag<const Object*> object_bag;
        Bag<const Object*> white_bag;
        Bag<const Object*> black_bag;
        std::vector<const Object*> gray_stack;
        Bag<const Object*> red_bag;

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
    
    
    
    
    void* Object::operator new(size_t count) {
        void* ptr = calloc(count, 1);
        thread_local_mutator->mutator_log.bytes_allocated += count;
        return ptr;
    }

    void Object::operator delete(void* ptr) {
        free(ptr);
    }

    Object::Object()
    : color() {
        thread_local_mutator->mutator_log.allocations.push(this);
    }
    
    void Object::_object_shade() const {
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
                _object_debug();
                abort();
                break;
        }
    }

    void Object::_object_trace() const {
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
                _object_debug();
                abort();
                break;
        }
    }
    
    void Object::_object_trace_weak() const {
        _object_trace();
    }
    
    
    Color Object::_object_sweep() const {
        return color.load();
    }
    
    void object_shade(const Object* object) {
        if (object)
            object->_object_shade();
    }
    
    void object_trace(const Object* object) {
        if (object)
            object->_object_trace();
    }

    void object_trace_weak(const Object* object) {
        if (object)
            object->_object_trace_weak();
    }
    
    
    
    const HeapString* HeapString::make(size_t hash, string_view view) {
        return global_collector->string_ctrie->find_or_emplace(_ctrie::Query{hash, view});
    }

    Color HeapString::_object_sweep() const {
        // Try to condemn the string to the terminal RED state
        Color expected = Color::WHITE;
        if (color.compare_exchange(expected, Color::RED)) {
            global_collector->string_ctrie->erase(this);
            return Color::RED;
        } else {
            return expected;
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
        if (expected.tag == Channel::Tag::COLLECTOR_DID_REQUEST_WAKEUP) {
            channel->log_stack_head.notify_one();
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
        int encoding = atomic_encoded_color_encoding.load(Ordering::RELAXED);
        atomic_encoded_color_encoding.store(encoding ^ 1, Ordering::RELAXED);
    }
    
    void Collector::set_alloc_to_black() {
        int encoding = atomic_encoded_color_encoding.load(Ordering::RELAXED);
        int encoded_black = (int)Color::BLACK ^ encoding;
        atomic_encoded_color_alloc.store(encoded_black, Ordering::RELAXED);
    }
    
    void Collector::consume_log_list(LogNode* log_list_head) {
        while (log_list_head) {
            collector_log.splice(std::move(*log_list_head));
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
        auto first = active_channels.begin();
        auto last = active_channels.end();
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
                    /*
                    (void) channel->log_stack_head.wait(expected,
                                                        Ordering::ACQUIRE);
                     */
                    switch (channel->log_stack_head.wait_for(expected,
                                                            Ordering::ACQUIRE,
                                                             1000000000)) {
                        case AtomicWaitStatus::NO_TIMEOUT:
                            break;
                        case AtomicWaitStatus::TIMEOUT:
                            printf("Mutator timed out\n");
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
                    TaggedPtr desired((LogNode*)nullptr, Channel::Tag::NOTHING);
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
        
        // Handshake ourself and shade our own root gc::objects
        this->handshake();
        object_shade(string_ctrie);
        
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
                    const Object* object = object_bag.top();
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
                            object->_object_scan();
                            [[fallthrough]];
                        case Color::BLACK:
                            // Is BLACK and will remain so
                            black_bag.push(object);
                            break;
                        case Color::RED:
                        default:
                            // "Impossible"
                            object_debug(object);
                            abort();
                    }
                    while (!gray_stack.empty()) {
                        // Depth first tracing
                        const Object* object = gray_stack.back();
                        gray_stack.pop_back();
                        assert(object);
                        object->_object_scan();
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
                const Object* object = object_bag.top();
                object_bag.pop();                
                switch (object->_object_sweep()) {
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
                        object_debug(object);
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
                const Object* object = red_bag.top();
                red_bag.pop();
                delete object;
            }
            
            // All mutators are allocating WHITE
            // Write barrier turns WHITE objects GRAY or BLACK
            // There are no RED objects
            assert(red_bag.empty());
            
        } // for(;;)
        
    } // void Collector::collect()
    
    
    
    void collector_start() {
        assert(global_collector == nullptr);
        global_collector = new gc::Collector;
        thread_local_mutator = global_collector;
        thread_local_mutator->enter();
        global_collector->string_ctrie = new Ctrie;
        thread_local_mutator->leave();
        thread_local_mutator = nullptr;
        std::thread([](){
            assert(!thread_local_mutator);
            thread_local_mutator = global_collector;
            global_collector->collect();
        }).detach();
    }
    
    void collector_stop() {
        // TODO: Clean shutdown.
        //
        // Communicate via a new Channel::Tag state?
        
        abort();
    }
    
    bool collector_this_thread_is_collector_thread() {
        return thread_local_mutator == global_collector;
    }
    
    
    void Object::_object_debug() const { abort(); }
    size_t Object::_object_hash() const { abort(); }
    
    std::strong_ordering Object::operator<=>(const Object&) const { abort(); }
    bool Object::operator==(const Object&) const { abort(); }

    
    define_test("gc") {
        std::thread([](){
            assert(!thread_local_mutator);
            mutator_enter();
            for (int i = 0; i != 100; ++i) {
                auto p = new HeapInt64(787);
                
                foo();

                mutator_handshake();
                object_shade(p);
            }
            mutator_leave();
            delete exchange(thread_local_mutator, nullptr);
        }).detach();
    };
    
    
    
} // namespace wry::gc


namespace wry {
    
    Atomic<gc::Encoded<gc::Color>>::Atomic()
    : _encoded_color(gc::global_collector->atomic_encoded_color_alloc.load(Ordering::RELAXED)) {
    }
    
    gc::Color Atomic<gc::Encoded<gc::Color>>::load() const {
        int encoding = gc::global_collector->atomic_encoded_color_encoding.load(Ordering::RELAXED);
        int encoded_discovered = _encoded_color.load(Ordering::RELAXED);
        return gc::Color{encoded_discovered ^ encoding};
    }
    
    bool Atomic<gc::Encoded<gc::Color>>::compare_exchange(gc::Color &expected, gc::Color desired) {
        int encoding = gc::global_collector->atomic_encoded_color_encoding.load(Ordering::RELAXED);
        int encoded_expected = (int)expected ^ encoding;
        int encoded_desired = (int)desired ^ encoding;
        bool result = _encoded_color.compare_exchange_strong(encoded_expected,
                                                             encoded_desired,
                                                             Ordering::RELAXED,
                                                             Ordering::RELAXED);
        expected = gc::Color{encoded_expected ^ encoding};
        return result;
    }
    
    
    
} // namespace wry
