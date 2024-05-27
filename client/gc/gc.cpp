//
//  gc.cpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#include "gc.hpp"

namespace gc {
    
    void Mutator::enter() {
        assert(_channel == nullptr);
        _channel = new Channel;
        auto expected = global_collector->channel_list_head.load(std::memory_order_acquire);
        for (;;) {
            auto next = expected;
            next.tag = Channel::MUTATOR_DID_ENTER;
            _channel->next.store(next);
            detail::TaggedPointer<Channel> desired(_channel);
            if (global_collector
                ->channel_list_head
                .compare_exchange_strong(expected,
                                         desired,
                                         std::memory_order_release,
                                         std::memory_order_acquire))
                break;
        }
        // the Collector store to _atomic_palette happens-before
        // the Collector's store-release to _channel_list_head happens-before
        // the Mutator's load-acquire of the _channel_list_head happens-before
        // the Mutator load of the _atomic_palette
        _channel->palette = global_collector->_atomic_palette.load(std::memory_order_relaxed);
    }
    
    void Mutator::leave() {
        assert(_channel);
        _channel->log.splice(std::move(this->log));
        auto expected = _channel->next.load(std::memory_order_relaxed);
        for (;;) {
            auto desired = expected;
            desired.tag = Channel::MUTATOR_DID_LEAVE;
            if (_channel->next.compare_exchange_strong(expected, 
                                                       desired,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed))
                break;
        }
        // Collector may now delete _channel
        _channel = nullptr;
        if (expected.tag == Channel::COLLECTOR_DID_WAIT) {
            // wrinkle: we need to wake the collector on the atomic we just
            // wrote, but if the collector saw the write already it may have
            // deleted said atomic
            //
            // collector can wait on something else like a conventional
            // condition variable, but then we have to take a lock rendeing the
            // fancy list above moot
            assert(false);
        }
    }
    
    void Mutator::handshake() {
        assert(_channel);
        auto expected = _channel->next.load(std::memory_order_acquire);
        switch (expected.tag) {
            case Channel::MUTATOR_DID_ENTER:
            case Channel::MUTATOR_DID_PUBLISH:
                // Collector doesn't need anything from us yet
                return;
            case Channel::COLLECTOR_DID_QUERY:
            case Channel::COLLECTOR_DID_WAIT:
                break;
            default:
                abort();
        }
        // The collector can now
        // - change next.ptr
        // - change the tag QUERY -> WAIT
        
        // update the local colors
        palette = _channel->palette;
        // publish local state
        // since the collector was the last thread to change the Channel, it
        // should have been emptied and cleared
        _channel->log.splice(std::move(this->log));

        for (;;) {
            auto desired = expected;
            desired.tag = Channel::MUTATOR_DID_PUBLISH;
            if (_channel->next.compare_exchange_strong(expected,
                                                       desired,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed))
                // finished
                break;
            // Collector changed the pointer or changed the tag to wait
            assert((expected.ptr != desired.ptr) || (expected.tag == Channel::COLLECTOR_DID_WAIT));
        }
        if (expected.tag == Channel::COLLECTOR_DID_WAIT) {
            // wait how?
            assert(false);
        }
    }
    
    
    void Collector::collect() {
        Mutator::enter();
        
        auto idempotent_sync = [&](Channel* b) {
            b->palette = this->palette;
            this->log.splice(std::move(b->log));
        };
        
        for (;;) {
            
            {
                // Transition local alloc color from white to black
                assert(palette.alloc == palette.white);
                palette.alloc = palette.black;
                
                // Publish the local palette
                _atomic_palette.store(palette, std::memory_order_relaxed);
                
                // Establish happens-before with incoming mutators
                {
                    auto expected = channel_list_head.load(std::memory_order_relaxed);
                    for (;;) {
                        if (channel_list_head.compare_exchange_strong(expected,
                                                                      expected,
                                                                      std::memory_order_release,
                                                                      std::memory_order_relaxed))
                            break;
                    }
                }
                // Any mutators joining after this point will start with the
                // new palette
                
                {
                    // Request handshake from everybody
                    auto* a = &channel_list_head;
                    auto b = a->load(std::memory_order_acquire);
                    // We can now read b->next
                    for (;;) {
                        if (!b.ptr)
                            // End of the list
                            break;
                        auto c = b->next.load(std::memory_order_acquire);
                        // We can now read b->*
                        if (c.tag == Channel::MUTATOR_DID_LEAVE) {
                            // idempotent sync
                            idempotent_sync(b.ptr);
                            // now attempt to delete the node
                            auto desired = b;
                            desired.ptr = c.ptr;
                            a->compare_exchange_strong(b,
                                                       desired,
                                                       std::memory_order_relaxed,
                                                       std::memory_order_acquire);
                            // either way, consider the node "a" now points to
                            continue;
                        }
                        // Now publish
                        b->palette = palette;
                        auto desired = c;
                        desired.tag = Channel::COLLECTOR_DID_QUERY;
                        
                        
                    }
                }
                
            }
            
            
            
            
            
            
        }
        
    }
    
    
} // namespace gc
