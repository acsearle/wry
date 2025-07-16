//
//  gc.hpp.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

namespace wry {
    
    struct GarbageCollected;

    
    // Collector
    
    void collector_acknowledge_child(void* tracer, const GarbageCollected* child);
    void collector_run_on_this_thread_until(std::chrono::steady_clock::time_point collector_deadline);
    inline std::atomic<ptrdiff_t> total_deleted = 0;
    
    // Mutator
    
    void mutator_become_with_name(const char*);
    void mutator_handshake(bool is_done = false);
    void mutator_did_overwrite(const GarbageCollected*);
    void mutator_declare_root(const GarbageCollected*);


}

#if 0

namespace wry {
    
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
    
} // namespace wry

#endif

#endif /* gc_hpp */
