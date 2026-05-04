//
//  global_work_queue.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include "global_work_queue.hpp"

#include "atomic.hpp"
#include "bump_allocator.hpp"
#include "concurrent_queue.hpp"
#include "garbage_collected.hpp"


namespace wry {

    BlockingDeque<void*> global_work_queue;

    namespace {

        // TODO: bump-allocator slab hand-off
        //
        // The thread_local `bump::this_thread_state` aborts on its destructor
        // if it still owns slabs at thread exit (by deliberate design, see
        // bump_allocator.hpp).  At present a worker thread can finish servicing
        // and exit while bump-allocated objects it produced are still in use
        // elsewhere — typically because the work it just ran allocated objects
        // visible to other threads via the GC heap.
        //
        // The proper fix is to hand the slab list off to an owner that keeps
        // it alive long enough: the garbage collector (which can free a slab
        // after a handshake confirms no thread holds a pointer into it), or
        // an arena pool that other threads can pull from.  Until that exists,
        // we leak: each exiting worker prepends its slab list onto a global
        // singly-linked list and never frees it.  The leak is bounded by the
        // number of workers that have ever run.
        Atomic<bump::Slab*> _orphaned_bump_slabs{};

        void _leak_this_thread_bump_slabs() {
            bump::Slab* mine = bump::this_thread_state.exchange_head_and_restart(nullptr);
            if (!mine)
                return;
            bump::Slab* tail = mine;
            while (tail->_next)
                tail = tail->_next;
            bump::Slab* expected = _orphaned_bump_slabs.load_relaxed();
            do {
                tail->_next = expected;
            } while (!_orphaned_bump_slabs.compare_exchange_weak_relaxed_relaxed(expected, mine));
        }

    } // anonymous namespace
    
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
            snprintf(str, size, "W%d", thread_identifier.fetch_add_relaxed(1));
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
        // Hand off (i.e. leak) any slabs this thread still owns so that the
        // bump::State destructor doesn't abort.  See _leak_this_thread_bump_slabs.
        _leak_this_thread_bump_slabs();
        // TODO: We can safely free the slabs after a certain number of epochs
        // have passed.  This is a use case for a "global_work_queue_schedule_after_epoch(epoch, ...)"
    }
    
}
