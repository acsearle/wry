//
//  gc.hpp.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

#include <chrono>

namespace wry {
    
    struct GarbageCollected;
    
    // Collector
    
    void collector_run_on_this_thread_until(std::chrono::steady_clock::time_point collector_deadline);
    
    // Mutator
    
    void mutator_become_with_name(const char*);
    void mutator_handshake();
    void mutator_resign();
    void mutator_overwrote(const GarbageCollected* old_ptr);
    void mutator_mark_root(const GarbageCollected* root_ptr);

} // namespace wry

#endif /* gc_hpp */
