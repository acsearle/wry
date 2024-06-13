//
//  gc.cpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#include <thread>

#include "gc.hpp"
#include "value.hpp"

#include "test.hpp"

namespace gc {
    
    void trace(const Object*);
    std::size_t gc_bytes(const Object*);
    void gc_enumerate(const Object*);
    void _gc_trace(const Object*);
    void _gc_delete(const Object*);

    
    struct Log {
        
        bool dirty;
        Bag<const Object*> allocations;
        std::intptr_t total;
        
        Log()
        : dirty(false)
        , allocations()
        , total(0) {}
        
        Log(const Log&) = delete;
        
        Log(Log&& other)
        : dirty(std::exchange(other.dirty, false))
        , allocations(std::move(other.allocations))
        , total(std::exchange(other.total, 0)) {
            assert(other.allocations.empty());
        }
        
        ~Log() {
            assert(!dirty);
            assert(allocations.empty());
            assert(total == 0);
        }
        
        Log& operator=(const Log&) = delete;
        
        Log& operator=(Log&&) = delete;
        
        Log& splice(Log&& other) {
            dirty = std::exchange(other.dirty, false) || dirty;
            allocations.splice(std::move(other.allocations));
            total += std::exchange(other.total, 0);
            return *this;
        }
        
    };
    
    struct LogNode : Log {
        LogNode* log_stack_next;
    };
    
    
    struct Channel {
        
        // A Channel is shared by one Mutator and one Collector.  They must
        // both release it before it is deleted.
        Atomic<std::intptr_t> reference_count_minus_one{1};
        Channel* entrant_stack_next = nullptr;
        enum Tag : std::intptr_t {
            COLLECTOR_DID_REQUEST_NOTHING, // -> HANDSHAKE, LEAVE
            COLLECTOR_DID_REQUEST_HANDSHAKE, // -> WAKEUP, LOGS, LEAVE
            COLLECTOR_DID_REQUEST_WAKEUP, // -> LOGS, LEAVE
            MUTATOR_DID_PUBLISH_LOGS, // -> NOTHING, LEAVE
            MUTATOR_DID_LEAVE, // -> .
        };
        
        // Palette palette;
        // std::atomic<TaggedPtr<LogNode>> log_stack_head;
        Atomic<TaggedPtr<LogNode>> log_stack_head;
        
        void release() {
            if (reference_count_minus_one.fetch_sub(1, Order::RELEASE) == 0) {
                (void) reference_count_minus_one.load(Order::ACQUIRE);
                delete this;
            }
        }
        
        
        
        
    };
    
    // garbage collector state for one mutator thread
    
    struct Mutator {
        
        // Thread-local state access point
        
        static thread_local Mutator* _thread_local_context_pointer;
        
        static Mutator& get();
        [[nodiscard]] static Mutator* _exchange(Mutator*);
        
        // Thread local state
        
        // Palette palette; // colors received from Collector
        Log     log    ; // activity to publish to Collector
        
        void _white_to_gray(Atomic<Color>& color);
        bool _white_to_black(Atomic<Color>& color) const;
        // template<typename T> T* write(std::atomic<T*>& target, std::type_identity_t<T>* desired);
        // template<typename T> T* write(std::atomic<T*>& target, std::nullptr_t);
        
        
        
        void* _allocate(std::size_t bytes);
        
        // Mutator endures for the whole thread lifetime
        // Channel is per enter-leave pairing
        // Channel must outlive not just leave, but the shutdown of the thread
        
        
        
        Channel* _channel = nullptr;
        
        void _publish_with_tag(Channel::Tag tag);
        
        void enter();
        void handshake();
        void leave();
        
    };
    
    // garbage collector state for the unique collector thread, which is
    // also a mutator
    
    struct Collector : Mutator {
        
        void visit(const Object* object);
        template<typename T> void visit(const Atomic<T*>& object);
        template<typename T> void visit(const Traced<T>& object);
        
        // Safety:
        // _scan_stack is only resized by the Collector thread, which is not
        // real time bounded
        std::vector<const Object*> _scan_stack;
        
        // These details can be done by a private class
        
        // lockfree channel
        // std::atomic<TaggedPtr<Channel>> channel_list_head;
        Atomic<Channel*> entrant_stack_head;
        // Atomic<Palette> _atomic_palette;
        Atomic<Color> _atomic_white;
        Atomic<Color> _atomic_alloc;
        
        std::vector<Channel*> _active_channels;
        
        Log _collector_log;
        
        void _process_scan_stack() {
            while (!this->_scan_stack.empty()) {
                const Object* object = this->_scan_stack.back();
                this->_scan_stack.pop_back();
                assert(object);
                gc_enumerate(object);
            }
        }
        
        void collect();
        
        void _set_alloc_to_black();
        void _swap_white_and_black();
        
        void _publish_palette_and_accept_entrants();
        
        void _initiate_handshakes();
        void _consume_logs(LogNode* logs);
        void _finalize_handshakes();
        void _synchronize();
        
        
        
        
    };
    
    Collector* global_collector = nullptr;
    

   
    
    
    // StrongPtr is a convenience wrapper that implements the write barrier for
    // a strong reference.
    
    
    void* Mutator::_allocate(std::size_t bytes) {
        // Safety: nonvirtual base
        Object* object = (Object*) malloc(bytes);
        // Safety: we don't use or publish these pointers until we handshake
        // by which time the objects are fully constructed
        this->log.allocations.push(object);
        this->log.total += bytes;
        return object;
    }
    
    void Mutator::_white_to_gray(Atomic<Color>& color) {
        Color expected = global_collector->_atomic_white.load(Order::RELAXED);
        if (color.compare_exchange_strong(expected,
                                          COLOR_GRAY,
                                          Order::RELAXED,
                                          Order::RELAXED)) {
            //if (!std::exchange(this->log.dirty, true))
              //  printf("Mutator became dirty\n");
        }
    }
    
    bool Mutator::_white_to_black(Atomic<Color>& color) const {
        Color expected = global_collector->_atomic_white.load(Order::RELAXED);
        return color.compare_exchange_strong(expected,
                                             color_invert(expected),
                                             Order::RELAXED,
                                             Order::RELAXED);
    }
    
    
    void Collector::visit(const Object* object) {
        if (object)
            _gc_trace(object);
    }
    
    template<typename T>
    void Collector::visit(const Atomic<T*>& participant) {
        this->visit(participant.load(Order::ACQUIRE));
    }
    
    template<typename T>
    void Collector::visit(const Traced<T>& p) {
        this->visit(p._ptr.load(Order::ACQUIRE));
    }
    
    
    
    
    
    
    void* Object::operator new(std::size_t count) {
        return Mutator::get()._allocate(count);
    }
    
    Object::Object(Class class_)
    : _class(class_)
    , _color(global_collector->_atomic_alloc.load(Order::RELAXED)) {
    }
    
    
    
    // Object virtual methods
    

    
    thread_local Mutator* Mutator::_thread_local_context_pointer;
    
    Mutator& Mutator::get() {
        return *_thread_local_context_pointer;
    }

    Mutator* Mutator::_exchange(Mutator* desired) {
        return std::exchange(_thread_local_context_pointer, desired);
    }
    
    void Mutator::enter() {
        assert(_channel == nullptr);
        _channel = new Channel;
        auto& target = global_collector->entrant_stack_head;
        auto& expected = _channel->entrant_stack_next;
        expected = target.load(Order::ACQUIRE);
        for (;;) {
            if (target.compare_exchange_strong(expected,
                                               _channel,
                                               Order::RELEASE,
                                               Order::ACQUIRE))
                break;
        }
        //this->palette.white = global_collector->_atomic_white.load(Order::RELAXED);
        //this->palette.alloc = global_collector->_atomic_alloc.load(Order::RELAXED);
    }
    
    void Mutator::_publish_with_tag(Channel::Tag tag) {
        assert(_channel);
        LogNode* node = new LogNode;
        //if (this->log.dirty) {
        //    printf("Mutator becomes clean\n");
        //}
        node->splice(std::move(this->log));
        assert(this->log.dirty == false);
        TaggedPtr<LogNode> desired(node, tag);
        TaggedPtr<LogNode> expected(_channel->log_stack_head.load(Order::ACQUIRE));
        for (;;) {
            node->log_stack_next = expected.ptr;
            if (_channel->log_stack_head.compare_exchange_strong(expected,
                                                                 desired,
                                                                 Order::RELEASE,
                                                                 Order::ACQUIRE))
                break;
        }
        if (expected.tag == Channel::COLLECTOR_DID_REQUEST_WAKEUP) {
            _channel->log_stack_head.notify_one();
            /*
            os_sync_wake_by_address_any(&(_channel->log_stack_head),
                                        8,
                                        OS_SYNC_WAKE_BY_ADDRESS_NONE);
             */
        }
    }

    void Mutator::handshake() {
        TaggedPtr<LogNode> expected(_channel->log_stack_head.load(Order::ACQUIRE));
        switch (expected.tag) {
            case Channel::COLLECTOR_DID_REQUEST_NOTHING:
                return;
            case Channel::COLLECTOR_DID_REQUEST_HANDSHAKE:
            case Channel::COLLECTOR_DID_REQUEST_WAKEUP:
                break;
            case Channel::MUTATOR_DID_PUBLISH_LOGS:
                return;
            case Channel::MUTATOR_DID_LEAVE:
            default:
                abort();
        }
        // we need to handshake
        //this->palette = _channel->palette;
        this->_publish_with_tag(Channel::MUTATOR_DID_PUBLISH_LOGS);
    }

    void Mutator::leave() {
        this->_publish_with_tag(Channel::MUTATOR_DID_LEAVE);
        std::exchange(this->_channel, nullptr)->release();
    }
    
    
    
    void Collector::_set_alloc_to_black() {
        Color white = _atomic_white.load(Order::RELAXED);
        Color black = (Color)(white ^ 1);
        _atomic_alloc.store(black, Order::RELAXED);
        
    }
    
    void Collector::_swap_white_and_black() {
        Color white = _atomic_white.load(Order::RELAXED);
        Color black = (Color)(white ^ 1);
        _atomic_white.store(black, Order::RELAXED);
    }

    
    
    void Collector::_initiate_handshakes() {
        std::vector<Channel*> survivors;
        
        for (;;) {
            if (this->_active_channels.empty())
                break;
            Channel* channel = _active_channels.back();
            assert(channel);
            _active_channels.pop_back();
            TaggedPtr<LogNode> desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_HANDSHAKE);
            TaggedPtr<LogNode> old = channel->log_stack_head.exchange(desired, Order::ACQ_REL);
            switch (old.tag) {
                case Channel::COLLECTOR_DID_REQUEST_NOTHING: {
                    survivors.push_back(channel);
                } break;
                case Channel::MUTATOR_DID_LEAVE: {
                    LogNode* head = old.ptr;
                    assert(head);
                    while (head) {
                        _collector_log.splice(std::move(*head));
                        delete std::exchange(head, head->log_stack_next);
                    }
                    std::exchange(channel, nullptr)->release();
                } break;
                default:
                    abort();
            }
        }
        std::swap(_active_channels, survivors);
    }
    
    
    void Collector::_consume_logs(LogNode* head) {
        while (head) {
            this->_collector_log.splice(std::move(*head));
            delete std::exchange(head, head->log_stack_next);
        }
    }
    
    void Collector::_finalize_handshakes() {
        std::vector<Channel*> survivors;
        for (;;) {
            if (this->_active_channels.empty())
                break;
            Channel* channel = _active_channels.back();
            assert(channel);
            _active_channels.pop_back();
            TaggedPtr<LogNode> expected;
        alpha:
            expected = channel->log_stack_head.load(Order::ACQUIRE);
        beta:
            switch (expected.tag) {
                case Channel::COLLECTOR_DID_REQUEST_HANDSHAKE: {
                    // The mutator has not yet responded
                    // Attempt to set the wakeup flag
                    auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_WAKEUP);
                    if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                        desired,
                                                                        Order::RELAXED,
                                                                        Order::ACQUIRE)) {
                        expected = desired;
                    }
                    // Start over with the new state
                    goto beta;
                }
                case Channel::COLLECTOR_DID_REQUEST_WAKEUP:
                    expected = channel->log_stack_head.wait(expected, Order::ACQUIRE);
                    goto beta;
                case Channel::MUTATOR_DID_PUBLISH_LOGS: {
                    // Mutator handshaked us
                    this->_consume_logs(expected.ptr);
                    auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_NOTHING);
                    if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                        desired,
                                                                        Order::RELAXED,
                                                                        Order::ACQUIRE)) {
                        survivors.push_back(channel);
                        break;
                    } else {
                        goto beta;
                    }
                }
                case Channel::MUTATOR_DID_LEAVE: {
                    this->_consume_logs(expected.ptr);
                    channel->release();
                    break;
                }
                default:
                    abort();
            } // switch(expected.tag)
        } // loop
        std::swap(_active_channels, survivors);
    }
    
    void Collector::_synchronize() {
        
        // Publish the atomic palette
        Channel* head = entrant_stack_head.exchange(nullptr, Order::ACQ_REL);
        
        // All entrants after this point will use the published palette
        while (head) {
            this->_active_channels.push_back(head);
            head = head->entrant_stack_next;
            // No processing of new entrants; they will have nothing to log,
            // unless they have already left too, which is handled below
        }
        
        // Use the channels to request that each mutator synchronizes with us
        // at its convenience
        this->_initiate_handshakes();
        
        // Handshake ourself
        this->handshake();
        
        // Wait for every mutator to handshake or leave
        this->_finalize_handshakes();
        
        
    }
    
    void Collector::collect() {
        
        Mutator::enter();
        
        Bag<const Object*> object_bag;
        Bag<const Object*> black_bag;
        Bag<const Object*> white_bag;
        
        for (;;) {
            
            // printf("Collector A\n");

            
            // All mutators are allocating WHITE
            // The write barrier is shading WHITE objects GRAY
            
            // Change alloc color from WHITE to BLACK
                        
            _set_alloc_to_black();
            Color white = global_collector->_atomic_white.load(Order::RELAXED);
            Color black = color_invert(white);
            _synchronize();
            object_bag.splice(std::move(_collector_log.allocations));
            _collector_log.dirty = false;
            
            // All mutators are allocating BLACK
            // The write barrier is turning WHITE objects GRAY (or BLACK)
            // All colors are present
            //
            // Scan for GRAY objects, shade their fields, and turn them BLACK
            
            
            assert(black_bag.empty());
            
            // printf("object_bag initial size is %zd\n", object_bag.count);
            
            for (;;) {
                
                for (;;) {
                    const Object* object = object_bag.pop();
                    if (!object)
                        break;
                    // load the color and set it to black if it was gray
                    // this briefly violates the tricolor invariant but the
                    // mutator threads are blind to shades of nonwhite and this
                    // collector thread immediately restores it
                    Color expected = COLOR_GRAY;
                    Color desired = black;
                    if (object->_color.compare_exchange_strong(expected, desired, Order::RELAXED, Order::RELAXED)) {
                        expected = desired;
                        gc_enumerate(object);
                    }
                    if (expected == black) {
                        black_bag.push(object);
                    } else if (expected == white) {
                        white_bag.push(object);
                    } else  {
                        abort();
                    }
                }
                assert(object_bag.empty());
                object_bag.swap(white_bag);
                
                _synchronize();
                if (!std::exchange(_collector_log.dirty, false)) {
                    break;
                }
                // Repeat until no new GRAY objects have been made during the
                // scan
                //
                // GRAY objects are made when a mutator thread write includes
                // a (non- Leaf) WHITE object for the first time since the last
                // BLACK to WHITE reinterpretation.
            }
            
            // All mutators are allocating BLACK
            // All mutators are clean
            // There are no GRAY objects
            //
            // Delete all WHITE objects
            
            {
                for (;;) {
                    const Object* object = object_bag.pop();
                    if (!object)
                        break;
                    _gc_delete(object);
                    // printf("Collector deletes something\n");
                }
            }
            
            object_bag.swap(black_bag);
            // printf("Survivors %zd\n", object_bag.size());
            
            // All mutators are allocating BLACK
            // There are no WHITE or GRAY objects
            //
            // Redefine WHITE as BLACK
            
            {
                _swap_white_and_black();
                _synchronize();

            }
            
            
            
            
            
            
            
        }
        
    }
    
    
    
    
    
    
    

    
    void trace(const Object* object) {
        if (object) {
            _gc_trace(object);
        }
    }
    
    void* allocate(std::size_t count) {
        return Mutator::get()._allocate(count);
    }

    

    
    
    
    void initialize_collector() {
        global_collector = new gc::Collector;
        auto old = gc::Mutator::_exchange(global_collector);
        assert(!old);
        std::thread([](){
            global_collector->collect();
        }).detach();
    }
    
    define_test("gc") {
        std::thread([](){
            auto m = new Mutator;
            auto old = Mutator::_exchange(m);
            assert(!old);
            m->enter();
            for (int i = 0; i != -1; ++i) {
                // printf("Mutator A\n");
                // auto p = new(m->_allocate(sizeof(Object))) Object(*m);
                auto p = new HeapInt64(787);
                
                foo();
                
                m->handshake();
                object_shade(p);
                // std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
            m->leave();
        }).detach();
    };
    
    
    
    
    
    

    

    
    
    void _gc_delete(const Object* object) {
        switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                const IndirectFixedCapacityValueArray* p = (const IndirectFixedCapacityValueArray*)object;
                // printf("%p del Traced<Value>[%zd]\n", p, p->_capacity);
                free(p->_storage);
                // printf("%p del IndirectFixedCapacityValueArray\n", p);
                delete p;
            } break;
            case CLASS_TABLE: {
                const HeapTable* p = (const HeapTable*)object;
                // printf("%p del HeapTable[%zd]\n", p, p->size());
                delete p;
            } break;
            case CLASS_STRING: {
                const HeapString* p = (const HeapString*)object;
                // printf("%p del HeapString"..."\n", p, p->size());
                delete p;
            } break;
            case CLASS_INT64: {
                const HeapInt64* p = (const HeapInt64*)object;
                // printf("%p del HeapInt64"..."\n", p, p->size());
                delete p;
            } break;
        }
    }
    
    std::size_t gc_bytes(const Object* object) {
        switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                const IndirectFixedCapacityValueArray* p = (const IndirectFixedCapacityValueArray*)object;
                return sizeof(IndirectFixedCapacityValueArray) + p->_capacity * sizeof(Traced<Value>);
            }
            case CLASS_TABLE: {
                return sizeof(HeapTable);
            }
            case CLASS_STRING: {
                const HeapString* p = (const HeapString*)object;
                return sizeof(HeapString) + p->_size;
            }
            case CLASS_INT64: {
                return sizeof(HeapInt64);
            }
            default: {
                abort();
            }
        }
    }
    
    
    std::size_t gc_hash(const Object* object) {
        switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case CLASS_TABLE: {
                return std::hash<const void*>()(object);
            }
            case CLASS_STRING: {
                const HeapString* p = (const HeapString*)object;
                return p->_hash;
            } break;
            case CLASS_INT64: {
                const HeapInt64* p = (const HeapInt64*)object;
                return std::hash<std::int64_t>()(p->_integer);
            }
            default: {
                abort();
            }
        }
    }
    
    
    void gc_enumerate(const Object* object) {
        switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                const IndirectFixedCapacityValueArray* p = (const IndirectFixedCapacityValueArray*)object;
                auto first = p->_storage;
                auto last = first + p->_capacity;
                for (; first != last; ++first) {
                    trace(*first);
                }
            } break;
            case CLASS_TABLE: {
                const HeapTable* p = (const HeapTable*)object;
                trace(p->_alpha._manager);
                trace(p->_beta._manager);
            } break;
            case CLASS_STRING:
            case CLASS_INT64:
                break;
        }
        
        
        
        
    }
    
    void object_shade(const Object* object){
        if (object) switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case CLASS_TABLE:
                Mutator::get()._white_to_gray(object->_color);
                break;
            case CLASS_STRING:
            case CLASS_INT64:
                Mutator::get()._white_to_black(object->_color);
                break;
        }
        

    }
    
    void _gc_trace(const Object* object) {
        Collector& context = *global_collector;
        switch (object->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case CLASS_TABLE:
                if (context._white_to_black(object->_color)) {
                    context._scan_stack.push_back(object);
                }
                break;
            case CLASS_STRING:
            case CLASS_INT64:
                abort();
        }
    }

    Color color_invert(Color x) {
        return (Color)(x ^ 1);
    }
    
    
    
    // Garbage Collector interface
    //
    // - Mutators are required to
    //   - execute a write barrier
    //   - log new allocations
    //   - periodically handshake with the mutator to
    //     - get new color scheme
    //     - report if there was at least one WHITE -> GRAY write barrier
    //     - report new allocations
    //   - mark any roots
    // - All these mutator actions are lock-free
    //   - the mutator will never wait for the collector
    //   - no GC pause, no stop the world, no stop the mutators in turn
    //   - the mutators can in theory outrun the collector and exhaust memory
    // - Where lock-free data structures are required, a very simple MPSC
    //   stack design is sufficient, implemented inline with minor variations
    //
    // - The system incurs significant overhead:
    //   - Barrier and allocator need access to thread-local state
    //     - Expensive to lookup on some architectures
    //     - Or, annoying to pass everywhere
    //   - All mutable pointers must be
    //     - atomic so the collector can read them
    //     - store-release so the collector can read through them
    //     - write-barrier so reachability is conservative
    //   - The write barrier adds two relaxed compare_exchanges to the object
    //     headers
    //   - Each object must store its color explicitly
    //   - Each object's address is explicitly stored either by a mutator in
    //     its list of recent allocations, or by the collector in its working
    //     list.  Together with color, this is 16 bytes per object of pure
    //     overhead.
    //   - All data structures must be quasi-concurrent so that they can be
    //     traced by the collector well enough to, in combination with the
    //     write barrier, produce a conservative reachability graph.
    //     - For example, for a fixed capacity allocation, we can't atomically
    //       consider both the size and the "back" element it implies; we have
    //       to rely on the immutable capacity, scan slot, and require the
    //       erase operation to leave unused elements in a traceable (preferably
    //       zeroed) state.
    //   - Unreachable objects will survive several rounds of handshakes due to
    //     the conservative nature of the collector; in particular they will
    //     survive the collection they were rendered unreachable during.
    
    // The collector is not lock-free.  It initiates rounds of "handshakes" with
    // the mutators and cannot progress until they have all responded at their
    // leisure.  In particular, it must wait for all mutators to report no GRAY
    // activity before tracing can terminate.   It maintains a list of
    // surviving objects and recent allocations to scan and sweep whenever
    // not waiting on handshakes.
    
    // An important optimization is to mark leaf objects -- objects with no
    // outgoing pointers to other GC objects -- directly to BLACK, skipping
    // the GRAY stage that indicates the collector must scan them.
    
    // Another important optimization is that, when the collector is scanning
    // its worklist of objects for GRAY objects that must be traced, it places
    // those child objects directly in a stack and then immediately traces
    // those, resulting in a depth-first traversal.  Without this optimization,
    // the collector would mark children GRAY and then scan to rediscover them;
    // a singly linked list appearing in reverse order in the worklist would
    // require O(N) scans, i.e. O(NM) operations to fully trace.
    
} // namespace gc
