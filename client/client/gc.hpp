//
//  gc.hpp
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
    void mutator_handshake();
    void mutator_leave();
    
} // namespace gc

#endif /* gc_hpp */
