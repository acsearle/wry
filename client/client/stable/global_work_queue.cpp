//
//  global_work_queue.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include "global_work_queue.hpp"

#include "concurrent_queue.hpp"
#include "garbage_collected.hpp"


namespace wry {
    
    BlockingDeque<void*> global_work_queue;
    
    // Note that while we wake one waiter when adding one work unit, we don't
    // reserve that work for that waiter; instead another thread might complete
    // a task and take that work.  This is fine.
    
    // TODO: Work queue
    //
    // This placeholder global queue is not particularly performant.  Replace
    // it with thread local work-stealing queues.
    //
    // The global blocking queue has the advantage of simplicity, particularly
    // for sleeping threads when idle.  Work out how to coordinate sleeping
    // without missed wakeups when there is no simple global definition of
    // empty.
    //
    // Sketch: after a thread has tried to steal from every queue, increment a
    // global sleep count.  Set a flag on each queue in turn.  Atomically sleep on
    // the expected global.  If a worker thread adds to its queue and discovers
    // the flag set, it increments the global and wakes sleepers?
    //
    // We want to fairly slowly throttle the number of threads up and down
    // rather than waking every single push.  We need some medium-term
    // estimate of the workload.
    
    // TODO: Work queue and fork order
    //
    // If we have a single thread and it pops the most recent job, we get
    // depth-first exploration of trees and a bound on the amount of jobs
    // waiting.
    
    
    void global_work_queue_cancel() {
        global_work_queue.cancel();
    }
    
    void global_work_queue_schedule(void* pointer) {
        assert(pointer);
        global_work_queue.push_back(pointer);
    }
        
    void global_work_queue_service() {
        {
            constinit static Atomic<int> thread_identifier{};
            size_t size = 256;
            char str[256];
            snprintf(str, size, "W%d", thread_identifier.fetch_add(1, Ordering::RELAXED));
            pthread_setname_np(str);
        }
        for (;;) {
            global_work_queue.wait_not_empty();
            if (global_work_queue.is_canceled())
                break;
            mutator_pin();
            void* callback = {};
            while (global_work_queue.try_pop_back(callback)) {
                assert(callback);
                (*(void(**)(void*))callback)(callback);
            }
            mutator_unpin();
        }
    }
    
}
