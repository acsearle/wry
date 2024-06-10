//
//  gc.cpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#include <os/os_sync_wait_on_address.h>

#include <thread>

#include "gc.hpp"
#include "value.hpp"

#include "test.hpp"

namespace gc {
    
    
    
    struct Palette {
        struct BlackImposter {
            Color white;
            operator Color() const { return white ^ 1; }
            //BlackImposter& operator=(Color value) { white = value ^ 1; return *this; }
        };
        union {
            struct {
                Color white;
            };
            BlackImposter black;
        };
        Color alloc;
        enum : Color {
            gray = 2,
            red = 3,
        };
    };
    
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
        std::atomic<std::intptr_t> reference_count_minus_one = 1;
        Channel* entrant_stack_next = nullptr;
        enum Tag : std::intptr_t {
            COLLECTOR_DID_REQUEST_NOTHING, // -> HANDSHAKE, LEAVE
            COLLECTOR_DID_REQUEST_HANDSHAKE, // -> WAKEUP, LOGS, LEAVE
            COLLECTOR_DID_REQUEST_WAKEUP, // -> LOGS, LEAVE
            MUTATOR_DID_PUBLISH_LOGS, // -> NOTHING, LEAVE
            MUTATOR_DID_LEAVE, // -> .
        };
        
        Palette palette;
        std::atomic<TaggedPtr<LogNode>> log_stack_head;
        
        void release() {
            if (reference_count_minus_one.fetch_sub(1, std::memory_order_release) == 0) {
                (void) reference_count_minus_one.load(std::memory_order_acquire);
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
        
        Palette palette; // colors received from Collector
        Log     log    ; // activity to publish to Collector
        
        void _white_to_gray(Atomic<Color>& color);
        bool _white_to_black(Atomic<Color>& color) const;
        void shade(const Object*);
        template<typename T> T* write(std::atomic<T*>& target, std::type_identity_t<T>* desired);
        template<typename T> T* write(std::atomic<T*>& target, std::nullptr_t);
        
        
        
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
        template<typename T> void visit(const std::atomic<T*>& object);
        template<typename T> void visit(const Traced<T>& object);
        
        // Safety:
        // _scan_stack is only resized by the Collector thread, which is not
        // real time bounded
        std::vector<const Object*> _scan_stack;
        
        // These details can be done by a private class
        
        // lockfree channel
        // std::atomic<TaggedPtr<Channel>> channel_list_head;
        std::atomic<Channel*> entrant_stack_head;
        std::atomic<Palette> _atomic_palette;
        
        std::vector<Channel*> _active_channels;
        
        Log _collector_log;
        
        void _process_scan_stack() {
            while (!this->_scan_stack.empty()) {
                const Object* object = this->_scan_stack.back();
                this->_scan_stack.pop_back();
                assert(object);
                object->gc_enumerate();
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
    
    
    
    void Mutator::shade(const Object* object) {
        if (object) {
            object->_gc_shade();
        }
    }
    
    // write barrier (non-trivial)
    template<typename T>
    T* Mutator::write(std::atomic<T*>& target, std::type_identity_t<T>* desired) {
        T* discovered = target.exchange(desired, std::memory_order_release);
        this->shade(desired);
        this->shade(discovered);
        return discovered;
    }
    
    template<typename T>
    T* Mutator::write(std::atomic<T*>& target, std::nullptr_t) {
        T* discovered = target.exchange(nullptr, std::memory_order_release);
        this->shade(discovered);
    }
    
    
    // StrongPtr is a convenience wrapper that implements the write barrier for
    // a strong reference.
    
    
    void* Mutator::_allocate(std::size_t bytes) {
        Object* object = (Object*) malloc(bytes);
        // Safety: we don't use or publish these pointers until we handshake
        // by which time the objects are fully constructed
        this->log.allocations.push(object);
        this->log.total += bytes;
        return object;
    }
    
    void Mutator::_white_to_gray(Atomic<Color>& color) {
        Color expected = this->palette.white;
        if (color.compare_exchange_strong(expected,
                                          this->palette.gray)) {
            this->log.dirty = true;
        }
    }
    
    bool Mutator::_white_to_black(Atomic<Color>& color) const {
        std::intptr_t expected = this->palette.white;
        return color.compare_exchange_strong(expected,
                                             this->palette.black);
    }
    
    
    void Collector::visit(const Object* object) {
        if (object)
            object->_gc_trace();
    }
    
    template<typename T>
    void Collector::visit(const std::atomic<T*>& participant) {
        this->visit(participant.load(std::memory_order_acquire));
    }
    
    template<typename T>
    void Collector::visit(const Traced<T>& p) {
        this->visit(p._ptr.load(std::memory_order_acquire));
    }
    
    
    void* Object::operator new(std::size_t count) {
        return Mutator::get()._allocate(count);
    }
    
    Object::Object()
    : _gc_color(Mutator::get().palette.alloc) {
    }
    
    Object::~Object() {
        // printf("%#" PRIxPTR " del Object\n", (std::uintptr_t) this);
    }
    
    std::size_t Object::gc_hash() const {
        return std::hash<const void*>()(this);
    }
    
    // Object virtual methods
    
    std::size_t Object::gc_bytes() const {
        return sizeof(Object);
    }
    
    void Object::gc_enumerate() const {
    }
    
    void Object::_gc_shade() const {
        Mutator::get()._white_to_gray(this->_gc_color);
    }
    
    void Object::_gc_trace() const {
        Collector& context = *global_collector;
        if (context._white_to_black(this->_gc_color)) {
            context._scan_stack.push_back(this);
        }
    }
    
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
        expected = target.load(std::memory_order_acquire);
        for (;;) {
            if (target.compare_exchange_strong(expected,
                                               _channel,
                                               std::memory_order_release,
                                               std::memory_order_acquire))
                break;
        }
        this->palette = global_collector->_atomic_palette.load(std::memory_order_relaxed);
    }
    
    void Mutator::_publish_with_tag(Channel::Tag tag) {
        assert(_channel);
        LogNode* node = new LogNode;
        node->splice(std::move(this->log));
        TaggedPtr<LogNode> desired(node, tag);
        TaggedPtr<LogNode> expected(_channel->log_stack_head.load(std::memory_order_acquire));
        for (;;) {
            node->log_stack_next = expected.ptr;
            if (_channel->log_stack_head.compare_exchange_strong(expected,
                                                                 desired,
                                                                 std::memory_order_release,
                                                                 std::memory_order_acquire))
                break;
        }
        if (expected.tag == Channel::COLLECTOR_DID_REQUEST_WAKEUP) {
            // _channel->log_stack_head.notify_one();
            os_sync_wake_by_address_any(&(_channel->log_stack_head),
                                        8,
                                        OS_SYNC_WAKE_BY_ADDRESS_NONE);
        }
    }

    void Mutator::handshake() {
        TaggedPtr<LogNode> expected(_channel->log_stack_head.load(std::memory_order_acquire));
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
        this->palette = _channel->palette;
        this->_publish_with_tag(Channel::MUTATOR_DID_PUBLISH_LOGS);
    }

    void Mutator::leave() {
        this->_publish_with_tag(Channel::MUTATOR_DID_LEAVE);
        std::exchange(this->_channel, nullptr)->release();
    }
    
    
    
    void Collector::_set_alloc_to_black() {
        Palette x = this->_atomic_palette.load(std::memory_order_relaxed);
        x.alloc = x.black;
        this->_atomic_palette.store(x, std::memory_order_relaxed);
    }
    
    void Collector::_swap_white_and_black() {
        Palette x = this->_atomic_palette.load(std::memory_order_relaxed);
        x.white ^= 1;;
        this->_atomic_palette.store(x, std::memory_order_relaxed);
    }

    
    
    void Collector::_initiate_handshakes() {
        std::vector<Channel*> survivors;
        
        for (;;) {
            if (this->_active_channels.empty())
                break;
            Channel* channel = _active_channels.back();
            assert(channel);
            _active_channels.pop_back();
            channel->palette = this->_atomic_palette.load(std::memory_order_relaxed);
            TaggedPtr<LogNode> desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_HANDSHAKE);
            TaggedPtr<LogNode> old = channel->log_stack_head.exchange(desired, std::memory_order_acq_rel);
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
            expected = channel->log_stack_head.load(std::memory_order_acquire);
        beta:
            switch (expected.tag) {
                case Channel::COLLECTOR_DID_REQUEST_HANDSHAKE: {
                    // The mutator has not yet responded
                    // Attempt to set the wakeup flag
                    auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_WAKEUP);
                    if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                        desired,
                                                                        std::memory_order_relaxed,
                                                                        std::memory_order_acquire)) {
                        expected = desired;
                    }
                    // Start over with the new state
                    goto beta;
                }
                case Channel::COLLECTOR_DID_REQUEST_WAKEUP:
                    // We are trying to sleep
                    // channel->log_stack_head.wait(expected, std::memory_order_relaxed);
                    // __ulock_wait();
                    os_sync_wait_on_address(&(channel->log_stack_head),
                                            expected._value,
                                            8,
                                            OS_SYNC_WAIT_ON_ADDRESS_NONE);
                    goto alpha;
                case Channel::MUTATOR_DID_PUBLISH_LOGS: {
                    // Mutator handshaked us
                    this->_consume_logs(expected.ptr);
                    auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_NOTHING);
                    if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                        desired,
                                                                        std::memory_order_relaxed,
                                                                        std::memory_order_acquire)) {
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
        Channel* head = entrant_stack_head.exchange(nullptr, std::memory_order_acq_rel);
        
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
                    Color expected = this->palette.gray;
                    Color desired = this->palette.black;
                    if (object->_gc_color.compare_exchange_strong(expected, desired)) {
                        expected = desired;
                        object->gc_enumerate();
                    }
                    if (expected == this->palette.black) {
                        black_bag.push(object);
                    } else if (expected == this->palette.white) {
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
                    delete object;
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
    
    
    
    
    
    
    void shade(const Object* object) {
        if (object) {
            Mutator::get().shade(object);
        }
    }
    
    void _gc_shade_for_leaf(Atomic<Color>* target) {
        auto& m = Mutator::get();
        Color expected = m.palette.white;
        Color desired = m.palette.black;
        target->compare_exchange_strong(expected,
                                        desired);
    }
    
    void trace(const Object* object) {
        if (object) {
            object->_gc_trace();
        }
    }
    
    void* allocate(std::size_t count) {
        return Mutator::get()._allocate(count);
    }

    
    Atomic<Color>::Atomic(Color color) : _color(color) {}
    
    Color Atomic<Color>::load() const {
        return _color.load(std::memory_order_relaxed);
    }
    
    bool Atomic<Color>::compare_exchange_strong(Color& expected, Color desired) {
        return _color.compare_exchange_strong(expected,
                                              desired,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed);
    }
    
    
    
    void initialize_collector() {
        global_collector = new gc::Collector;
        gc::Palette p;
        p.white = 0;
        p.alloc = 0;
        global_collector->_atomic_palette.store(p, std::memory_order_release);
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
                auto p = new Object;
                
                foo();
                
                m->handshake();
                p->_gc_shade();
                // std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
            m->leave();
        }).detach();
    };
    
    void shade(const Object* p, const Object* q) {
        // TODO: share TLS lookup
        shade(p);
        shade(q);
    }
    
} // namespace gc
