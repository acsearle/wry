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
    
    // Object virtual methods
    
    std::size_t Object::gc_bytes() const {
        return sizeof(Object);
    }
    
    void Object::gc_enumerate(Collector&) const {
    }
    
    void Object::_gc_shade(Mutator& context) const {
        context._white_to_gray(this->_gc_color);
    }
    
    void Object::_gc_trace(Collector& context) const {
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
        if (expected.tag == Channel::COLLECTOR_DID_REQUEST_WAKEUP)
            _channel->log_stack_head.notify_one();
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
                    channel->log_stack_head.wait(expected, std::memory_order_relaxed);
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
                        printf("Collector is happy\n");
                        break;
                    } else {
                        goto beta;
                    }
                }
                case Channel::MUTATOR_DID_LEAVE: {
                    this->_consume_logs(expected.ptr);
                    channel->release();
                    std::exchange(channel, nullptr)->release();
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
            
            printf("Collector A\n");

            
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
            
            printf("object_bag initial size is %zd\n", object_bag.count);
            
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
                    if (object->_gc_color.compare_exchange_strong(expected,
                                                                  desired,
                                                                  std::memory_order_relaxed,
                                                                  std::memory_order_relaxed)) {
                        expected = desired;
                        object->gc_enumerate(*this);
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
                    printf("Collector deletes something\n");
                }
            }
            
            object_bag.swap(black_bag);
            printf("Survivors %zd\n", object_bag.size());
            
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
    
    
    
    
    define_test("gc") {
        std::thread([](){
            global_collector = new Collector;
            Palette p;
            p.white = 0;
            p.alloc = 0;
            global_collector->_atomic_palette.store(p, std::memory_order_release);
            auto old = Mutator::_exchange(global_collector);
            assert(!old);

            std::thread([](){
                auto m = new Mutator;
                auto old = Mutator::_exchange(m);
                assert(!old);
                m->enter();
                for (;;) {
                    printf("Mutator A\n");
                    // auto p = new(m->_allocate(sizeof(Object))) Object(*m);
                    auto p = new Object;
                    
                    foo();
                    
                    m->handshake();
                    p->_gc_shade(*m);
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }
                m->leave();
            }).detach();
            
            global_collector->collect();
            
        }).detach();
    };
    
    
} // namespace gc
