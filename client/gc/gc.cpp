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
#include "table.hpp"
#include "tagged_ptr.hpp"
#include "utility.hpp"
#include "value.hpp"

#include "test.hpp"

namespace wry::gc {
        
    namespace {
        
        void object_scan(const Object*);
        void object_delete(const Object*);
        
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
            
            Atomic<TaggedPtr<LogNode>> log_stack_head;
            
            void release() {
                if (reference_count_minus_one.fetch_sub(1, Ordering::RELEASE) == 0) {
                    (void) reference_count_minus_one.load(Ordering::ACQUIRE);
                    delete this;
                }
            }
            
        };
        
        // garbage collector state for one mutator thread
        
        struct Mutator {
            
            Channel* _channel = nullptr;
            Log log; // activity to publish to Collector
            
            void _white_to_gray(Atomic<int>& atomic_encoded_color);
            // bool _white_to_black(Atomic<Color>& color) const;
            // template<typename T> T* write(std::atomic<T*>& target, std::type_identity_t<T>* desired);
            // template<typename T> T* write(std::atomic<T*>& target, std::nullptr_t);
            
            
            
            void* _allocate(std::size_t bytes);
            
            // Mutator endures for the whole thread lifetime
            // Channel is per enter-leave pairing
            // Channel must outlive not just leave, but the shutdown of the thread
            
            
            
            
            void _publish_with_tag(Channel::Tag tag);
            
            void enter();
            void handshake();
            void leave();
            
        };
        
        thread_local Mutator* thread_local_mutator = nullptr;
        
        // garbage collector state for the unique collector thread, which is
        // also a mutator
        
        struct Collector : Mutator {
            
            // Safety:
            // _gray_stack is only resized by the Collector thread, which is not
            // real time bounded
            std::vector<const Object*> _gray_stack;
            
            // These details can be done by a private class
            
            // lockfree channel
            // std::atomic<TaggedPtr<Channel>> channel_list_head;
            Atomic<Channel*> entrant_stack_head;
            
            Atomic<int> _atomic_color_encoding;
            Atomic<int> _atomic_encoded_alloc_color;
            
            std::vector<Channel*> _active_channels;
            
            Log _collector_log;
            
            Ctrie* _string_ctrie = nullptr;
            
            // TODO: the arbitrary-precision HeapInteger can be interned in
            // exactly the same way
            // Until then, abort() on overflow of 60 bit integers?
            // or even just make do with 32 bits?
            
            
            void _process_gray_stack() {
                while (!this->_gray_stack.empty()) {
                    const Object* object = this->_gray_stack.back();
                    this->_gray_stack.pop_back();
                    assert(object);
                    object_scan(object);
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
        
        
        
        
        
        
        void* Mutator::_allocate(std::size_t bytes) {
            // Safety: nonvirtual base
            Object* object = (Object*) malloc(bytes);
            // Safety: we don't use or publish these pointers until we handshake
            // by which time the objects are fully constructed
            this->log.allocations.push(object);
            this->log.total += bytes;
            return object;
        }
        
        void Mutator::_white_to_gray(Atomic<int>& atomic_encoded_color) {
            // Color expected = global_collector->_atomic_white.load(Ordering::RELAXED);
            int encoding = global_collector->_atomic_color_encoding.load(Ordering::RELAXED);
            int expected = (int)Color::WHITE ^ encoding;
            int desired = (int)Color::GRAY ^ encoding;
            if (atomic_encoded_color.compare_exchange_strong(expected,
                                                             desired,
                                                             Ordering::RELAXED,
                                                             Ordering::RELAXED)) {
                this->log.dirty = true;
            }
        }
        
        void Mutator::enter() {
            assert(_channel == nullptr);
            _channel = new Channel;
            auto& target = global_collector->entrant_stack_head;
            auto& expected = _channel->entrant_stack_next;
            expected = target.load(Ordering::ACQUIRE);
            for (;;) {
                if (target.compare_exchange_strong(expected,
                                                   _channel,
                                                   Ordering::RELEASE,
                                                   Ordering::ACQUIRE))
                    break;
            }
        }
        
        void Mutator::_publish_with_tag(Channel::Tag tag) {
            assert(_channel);
            LogNode* node = new LogNode;
            node->splice(std::move(this->log));
            assert(this->log.dirty == false);
            TaggedPtr<LogNode> desired(node, tag);
            TaggedPtr<LogNode> expected(_channel->log_stack_head.load(Ordering::ACQUIRE));
            for (;;) {
                node->log_stack_next = expected.ptr;
                if (_channel->log_stack_head.compare_exchange_strong(expected,
                                                                     desired,
                                                                     Ordering::RELEASE,
                                                                     Ordering::ACQUIRE))
                    break;
            }
            if (expected.tag == Channel::COLLECTOR_DID_REQUEST_WAKEUP) {
                _channel->log_stack_head.notify_one();
            }
        }
        
        void Mutator::handshake() {
            TaggedPtr<LogNode> expected(_channel->log_stack_head.load(Ordering::ACQUIRE));
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
            int encoding = _atomic_color_encoding.load(Ordering::RELAXED);
            int encoded_black = encoding ^ (int)Color::BLACK;
            _atomic_encoded_alloc_color.store(encoded_black, Ordering::RELAXED);
            
        }
        
        void Collector::_swap_white_and_black() {
            int encoding = _atomic_color_encoding.load(Ordering::RELAXED);
            _atomic_color_encoding.store(encoding ^ 1, Ordering::RELAXED);
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
                TaggedPtr<LogNode> old = channel->log_stack_head.exchange(desired, Ordering::ACQ_REL);
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
                expected = channel->log_stack_head.load(Ordering::ACQUIRE);
            beta:
                switch (expected.tag) {
                    case Channel::COLLECTOR_DID_REQUEST_HANDSHAKE: {
                        // The mutator has not yet responded
                        // Attempt to set the wakeup flag
                        auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_WAKEUP);
                        if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                            desired,
                                                                            Ordering::RELAXED,
                                                                            Ordering::ACQUIRE)) {
                            expected = desired;
                        }
                        // Start over with the new state
                        goto beta;
                    }
                    case Channel::COLLECTOR_DID_REQUEST_WAKEUP:
                        expected = channel->log_stack_head.wait(expected, Ordering::ACQUIRE);
                        goto beta;
                    case Channel::MUTATOR_DID_PUBLISH_LOGS: {
                        // Mutator handshaked us
                        this->_consume_logs(expected.ptr);
                        auto desired = TaggedPtr<LogNode>(nullptr, Channel::COLLECTOR_DID_REQUEST_NOTHING);
                        if (channel->log_stack_head.compare_exchange_strong(expected,
                                                                            desired,
                                                                            Ordering::RELAXED,
                                                                            Ordering::ACQUIRE)) {
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
            Channel* head = entrant_stack_head.exchange(nullptr, Ordering::ACQ_REL);
            
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
            
            // Shade our own roots
            object_shade(_string_ctrie);
            
            // Wait for every mutator to handshake or leave
            this->_finalize_handshakes();
            
            
        }
        
        void Collector::collect() {
            
            Mutator::enter();
            
            // For simplicity we keep a number of explicit bags
            
            Bag<const Object*> object_bag;
            Bag<const Object*> black_bag;
            Bag<const Object*> white_bag;
            Bag<const Object*> red_bag;
            
            for (;;) {
                
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
                
                for (;;) {
                    
                    while (!object_bag.empty()) {
                        const Object* object = object_bag.top();
                        object_bag.pop();
                        assert(object);
                        Color expected = Color::GRAY;
                        object_color_compare_exchange(object, expected, Color::BLACK);
                        switch (expected) {
                            case Color::WHITE:
                                white_bag.push(object);
                                break;
                            case Color::GRAY:
                                object_scan(object);
                                [[fallthrough]];
                            case Color::BLACK:
                                black_bag.push(object);
                                break;
                            case Color::RED:
                                object_debug(object);
                                abort();
                        }
                        _process_gray_stack();
                    }
                    
                    // Note that some of the objects we put in the white bag
                    // may have been turned GRAY by a mutator or BLACK by
                    // us when processing
                    
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
                    // Sweep
                    while (!object_bag.empty()) {
                        const Object* object = object_bag.top();
                        object_bag.pop();
                        
                        // TODO: pull this out into a function... still needs
                        // to route the object to red, black, or doom
                        switch (object->_class) {
                            case Class::STRING: {
                                Color expected = Color::WHITE;
                                object_color_compare_exchange(object,
                                                              expected,
                                                              Color::RED);
                                switch (expected) {
                                    case Color::WHITE:
                                        // Begin reclaiming the string
                                        _string_ctrie->erase((HeapString*)object);
                                        red_bag.push(object);
                                        break;
                                    case Color::BLACK:
                                        // Between the scan and the sweep a mutator claimed the string
                                        black_bag.push(object);
                                        break;
                                    case Color::GRAY:
                                    case Color::RED:
                                    default:
                                        object_debug(object);
                                        abort();
                                }
                                break;
                            }
                            default: {
                                switch (object_color_load(object)) {
                                    case Color::WHITE:
                                        object_delete(object);
                                        break;
                                    case Color::BLACK:
                                        black_bag.push(object);
                                        break;
                                    case Color::GRAY:
                                    case Color::RED:
                                    default:
                                        object_debug(object);
                                        abort();
                                        
                                }
                            }
                        }
                    }
                }
                
                object_bag.swap(black_bag);
                
                // All objects are BLACK or RED
                // All mutators are allocating BLACK
                // There are no WHITE or GRAY objects
                // Mutators may be dereferencing RED objects
                
                // Redefine WHITE as BLACK
                
                {
                    _swap_white_and_black();
                    _synchronize();
                    
                }
                
                // All mutators are allocating WHITE
                // Write barrier turns WHITE objects GRAY or BLACK
                // Mutators cannot discover RED objects
                
                // Delete all RED objects
                
                while (!red_bag.empty()) {
                    const Object* object = red_bag.top();
                    red_bag.pop();
                    object_delete(object);
                }
                
                // All mutators are allocating WHITE
                // Write barrier turns WHITE objects GRAY or BLACK
                // There are no RED objects
                assert(red_bag.empty());
                
            } // for(;;)
            
        } // void Collector::collect()
        
    } // namespace <anonymous>
    
    
    
    
    
    
    
    void* object_allocate(std::size_t count) {
        return thread_local_mutator->_allocate(count);
    }
    
    void* Object::operator new(std::size_t count) {
        return thread_local_mutator->_allocate(count);
    }
    
    Object::Object(Class class_)
    : _class(class_)
    , _color() {
    }
    
} // namespace wry::gc

namespace wry {
    
    Atomic<gc::Encoded<gc::Color>>::Atomic()
    : _encoded_color(gc::global_collector->_atomic_encoded_alloc_color.load(Ordering::RELAXED)) {
    }
    
    gc::Color Atomic<gc::Encoded<gc::Color>>::load() const {
        int encoding = gc::global_collector->_atomic_color_encoding.load(Ordering::RELAXED);
        int discovered = _encoded_color.load(Ordering::RELAXED);
        return gc::Color{discovered ^ encoding};
    }
    
    bool Atomic<gc::Encoded<gc::Color>>::compare_exchange(gc::Color &expected, gc::Color desired) {
        int encoding = gc::global_collector->_atomic_color_encoding.load(Ordering::RELAXED);
        int encoded_expected = (int)expected ^ encoding;
        bool result = _encoded_color.compare_exchange_strong(encoded_expected,
                                                             (int)desired ^ encoding,
                                                             Ordering::RELAXED,
                                                             Ordering::RELAXED);
        expected = gc::Color{encoded_expected ^ encoding};
        return result;
    }
    
} // namespace wry

namespace wry::gc {
    
   void collector_start() {
        global_collector = new gc::Collector;
        thread_local_mutator = global_collector;
        thread_local_mutator->enter();
        global_collector->_string_ctrie = new Ctrie;
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
    
    define_test("gc") {
        std::thread([](){
            assert(!thread_local_mutator);
            thread_local_mutator = new Mutator;
            thread_local_mutator->enter();
            for (int i = 0; i != -1; ++i) {
                auto p = new HeapInt64(787);
                
                foo();
                
                thread_local_mutator->handshake();
                object_shade(p);
            }
            thread_local_mutator->leave();
        }).detach();
    };
    
    
    
    
    
    

    

    
    
    std::size_t object_hash(const Object* object) {
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
                return std::hash<const void*>()(object);
            case Class::STRING:
                return ((const HeapString*)object)->_hash;
            case Class::INT64:
                return std::hash<std::int64_t>()(((const HeapInt64*)object)->_integer);
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE:
                abort();
        }
    }
    
    void object_shade(const Object* object){
        if (!object)
            return;
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE: {
                Color expected = Color::WHITE;
                (void) object_color_compare_exchange(object,
                                                     expected,
                                                     Color::GRAY);
                switch (expected) {
                    case Color::WHITE:
                        thread_local_mutator->log.dirty = true;
                    case Color::BLACK:
                    case Color::GRAY:
                        break;
                    case Color::RED:
                    default:
                        object_debug(object);
                        abort();
                        break;
                }
            } break;
            case Class::STRING:
            case Class::INT64: {
                Color expected = Color::WHITE;
                (void) object_color_compare_exchange(object, expected, Color::BLACK);
                break;
            }
            default: {
                object_debug(object);
                abort();
            }
        }
    }

    namespace {
        
        void object_scan(const Object* object) {
            assert(object);
            switch (object->_class) {
                case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                    const IndirectFixedCapacityValueArray* p = (const IndirectFixedCapacityValueArray*)object;
                    auto first = p->_storage;
                    auto last = first + p->_capacity;
                    for (; first != last; ++first)
                        value_trace(*first);
                    break;
                }
                case Class::TABLE: {
                    const HeapTable* p = (const HeapTable*)object;
                    object_trace(p->_alpha._manager);
                    object_trace(p->_beta._manager);
                    break;
                }
                case Class::STRING: 
                case Class::INT64:{
                    // These leaf classes should never be GRAY and thus never scanned
                    abort();
                }
                case Class::CTRIE: {
                    const Ctrie* p = (const Ctrie*)object;
                    object_trace(p->root);
                    break;
                }
                case Class::CTRIE_CNODE: {
                    const Ctrie::CNode* cn = (const Ctrie::CNode*)object;
                    int num = __builtin_popcountll(cn->bmp);
                    for (int i = 0; i != num; ++i)
                        object_trace_weak(cn->array[i]);
                    break;
                }
                case Class::CTRIE_INODE: {
                    const Ctrie::INode* in = (const Ctrie::INode*)object;
                    object_trace(in->main);
                    break;
                }
                case Class::CTRIE_LNODE: {
                    const Ctrie::LNode* ln = (const Ctrie::LNode*)object;
                    object_trace_weak(ln->sn);
                    object_trace(ln->next);
                    break;
                }
                    /*
                case Class::CTRIE_SNODE: {
                    const Ctrie::SNode* sn = (const Ctrie::SNode*)object;
                    value_trace(sn->key);
                    value_trace(sn->value);
                    break;
                }*/
                case Class::CTRIE_TNODE: {
                    const Ctrie::TNode* tn = (const Ctrie::TNode*)object;
                    object_trace_weak(tn);
                    break;
                }
            }
        }
        
    }
    
    
    void object_trace(const Object* object) {
        if (!object)
            return;
        Color expected = Color::WHITE;
        (void) object_color_compare_exchange(object,
                                             expected,
                                             Color::BLACK);
        switch (object->_class) {
            case Class::STRING:
            case Class::INT64:{
                switch (expected) {
                    case Color::WHITE:
                    case Color::BLACK:
                        break;
                    case Color::GRAY:
                    case Color::RED:
                    default:
                        object_debug(object);
                        abort();
                        break;
                }
                break;
            }
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE: {
                switch (expected) {
                    case Color::WHITE:
                        global_collector->_gray_stack.push_back(object);
                    case Color::BLACK:
                    case Color::GRAY:
                        break;
                    case Color::RED:
                    default:
                        object_debug(object);
                        abort();
                        break;
                }
            } break;
            default: {
                object_debug(object);
                abort();
            }
        }
    }
    
    void object_trace_weak(const Object* object) {
        if (!object)
            return;
        switch (object->_class) {
            case Class::STRING:
            case Class::INT64:{
                break;
            }
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
            case Class::TABLE:
            case Class::CTRIE:
            case Class::CTRIE_CNODE:
            case Class::CTRIE_INODE:
            case Class::CTRIE_LNODE:
            case Class::CTRIE_TNODE: {
                Color expected = Color::WHITE;
                (void) object_color_compare_exchange(object, expected, Color::BLACK);
                switch (expected) {
                    case Color::WHITE:
                        global_collector->_gray_stack.push_back(object);
                    case Color::BLACK:
                    case Color::GRAY:
                        break;
                    case Color::RED:
                    default:
                        object_debug(object);
                        abort();
                        break;
                }
                break;
            }
            default:
                object_debug(object);
                abort();
        }
    }
    
    namespace {
        
        void object_delete(const Object* object) {
            object_debug(object);
            if (object == nullptr)
                return;
            switch (object->_class) {
                case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
                    return delete (const IndirectFixedCapacityValueArray*)object;
                case Class::TABLE:
                    return delete (const HeapTable*)object;
                case Class::STRING:
                    return delete (const HeapString*)object;
                case Class::INT64:
                    return delete (const HeapInt64*)object;
                case Class::CTRIE:
                    return delete (const Ctrie*)object;
                case Class::CTRIE_CNODE:
                    return delete (const Ctrie::CNode*)object;
                case Class::CTRIE_INODE:
                    return delete (const Ctrie::INode*)object;
                case Class::CTRIE_LNODE:
                    return delete (const Ctrie::LNode*)object;
                case Class::CTRIE_TNODE:
                    return delete (const Ctrie::TNode*)object;
                default:
                    abort();
            }
        }
        
    }
    
    void object_debug(const Object* object) {
        if (!object)
            return (void)printf("%#0.12" PRIx64 "\n", (uint64_t)0);
        switch (object->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
                return (void)printf("IndirectFixedCapacityValueArray[%zd]\n",
                                    ((const IndirectFixedCapacityValueArray*)object)->_capacity);
            }
            case Class::TABLE: {
                return (void)printf("HeapTable[%zd]\n",
                                    ((const HeapTable*)object)->size());
            }
            case Class::STRING: {
                const HeapString* string = (const HeapString*)object;
                return (void)printf("%#0.12" PRIx64 "\"%.*s\"\n",
                                    (uint64_t)object,
                                    (int)string->_size,
                                    string->_bytes);
            }
            case Class::INT64: {
                return (void)printf("HeapInt64\n");
            }
            case Class::CTRIE: {
                return (void)printf("%#0.12" PRIx64 " Ctrie\n", (uint64_t)object);
            }
            case Class::CTRIE_CNODE: {
                return (void)printf("Ctrie::CNode{.bmp=%llx}\n", ((const Ctrie::CNode*)object)->bmp);
            }
            case Class::CTRIE_INODE: {
                return (void)printf("Ctrie::INode\n");
            }
            case Class::CTRIE_LNODE: {
                return (void)printf("Ctrie::INode\n");
            }
            case Class::CTRIE_TNODE: {
                return (void)printf("Ctrie::TNode\n");
            }
            default: {
                return (void)printf("Object{._class=%x}\n", object->_class);
            }
        }
    }
    
    
    HeapString* HeapString::make(size_t hash, string_view view) {
        return global_collector->_string_ctrie->find_or_emplace(Ctrie::Query{hash, view});
    }

    Color object_color_load(const Object* object) {
        return object->_color.load();
    }
    
    bool object_color_compare_exchange(const Object* object,
                                        Color& expected,
                                        Color desired) {
        return object->_color.compare_exchange(expected, desired);
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

    
    /*
     std::size_t gc_bytes(const Object* object) {
     switch (object->_class) {
     case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY: {
     const IndirectFixedCapacityValueArray* p = (const IndirectFixedCapacityValueArray*)object;
     return sizeof(IndirectFixedCapacityValueArray) + p->_capacity * sizeof(Traced<Value>);
     }
     case Class::TABLE: {
     return sizeof(HeapTable);
     }
     case Class::STRING: {
     const HeapString* p = (const HeapString*)object;
     return sizeof(HeapString) + p->_size;
     }
     case Class::INT64: {
     return sizeof(HeapInt64);
     }
     default: {
     abort();
     }
     }
     }
     */
    
    
    
    
} // namespace gc



