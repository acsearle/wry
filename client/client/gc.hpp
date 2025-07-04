//
//  gc.hpp.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

namespace wry::gc {
    
    void collector_start();
    bool collector_this_thread_is_collector_thread();
    void collector_stop();
    
    void mutator_enter();
    bool mutator_is_entered();
    
    enum class HandshakeResult {
        OK,
        COLLECTOR_DID_REQUEST_MUTATOR_LEAVES,
    };
    
    void mutator_handshake();
    void mutator_leave();
    
    void* allocate(size_t bytes);
    void deallocate(void* ptr, size_t bytes);
    
} // namespace gc

#endif /* gc_hpp */
