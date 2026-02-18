//
//  global_work_queue.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef global_work_queue_hpp
#define global_work_queue_hpp

namespace wry {
    
    // Simple global work queue
    //
    // Work is described by pointers that are invoked by
    //
    //     (*(void(**)(void*))ptr)(ptr);
    //
    // That is, a pointer to a function pointer that receives a void pointer to
    // its own location as its only argument.  It is often the first member of
    // a larger struct that is thus guaranteed to have the same address.
    //
    //     struct task {
    //         void (*f)(void*) = [](void* p) { auto q = (task*)p; p->stuff; ... };
    //         int stuff;
    //         ...
    //     }
    //
    // This gives us a type-erased function object identified a single pointer.
    //
    // Clang, GCC and MSVC coroutines currently follow this structure.  The work
    // queue does not use or require the destroy or promise parts of the
    // coroutine frame.
    //
    // Offset hacking can be used to recover the address of an object the
    // function pointer is not the first member of.
    //
    
    void global_work_queue_schedule(void*);
    
    void global_work_queue_service();
    void global_work_queue_cancel();
    

}

#endif /* global_work_queue_hpp */
