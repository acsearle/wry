//
//  coroutine.cpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>

#include "coroutine.hpp"

#include "execution.hpp"
#include "test.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<> handle) {
        global_work_queue_schedule(handle.address());
    }

    // -----------------------------------------------------------------------
    // WaitGroup: the single process-lifetime work anchor.  Hidden state; the
    // methods are the whole interface.  A shutdown-time wait, so a plain
    // mutex + condition_variable is plenty.
    // -----------------------------------------------------------------------

    namespace {
        std::mutex              g_wait_group_mutex;
        std::condition_variable g_wait_group_cv;
        // Starts at 1: a process sentinel held on behalf of main.  While the
        // count is non-zero the group is "open" and spawning is legal -- the
        // sentinel or in-flight work keeps it so, even when no tasks are
        // momentarily outstanding.  wait() releases the sentinel exactly once at
        // shutdown, which is what lets the count fall to zero.  A spawn that then
        // finds the count already at zero races wait()'s return and would be
        // abandoned, so add() aborts on it.
        std::ptrdiff_t          g_wait_group_count = 1;
    }

    void WaitGroup::add(std::ptrdiff_t n) {
        std::scoped_lock lock{g_wait_group_mutex};
        assert(g_wait_group_count != 0
               && "WaitGroup::add() after the group drained -- spawn/shutdown race");
        g_wait_group_count += n;
    }

    void WaitGroup::done() {
        bool became_zero;
        {
            std::scoped_lock lock{g_wait_group_mutex};
            assert(g_wait_group_count > 0);
            became_zero = (--g_wait_group_count == 0);
        }
        if (became_zero)
            g_wait_group_cv.notify_all();
    }

    void WaitGroup::wait() {
        std::unique_lock lock{g_wait_group_mutex};
        // Release the process sentinel -- exactly once; a second wait() would
        // underflow, which the assert catches.  Then block until work drains.
        assert(g_wait_group_count > 0 && "WaitGroup::wait() called more than once");
        --g_wait_group_count;
        g_wait_group_cv.wait(lock, [] { return g_wait_group_count == 0; });
    }

    // Wrap `task` so the group count is released only after it fully completes
    // (across its internal yields).  The trailing SuspendAndDestroy is NOT
    // redundant with Future::final_suspend: final_suspend destroys the frame
    // but also resumes the promise's continuation, which is null for this
    // detached runner (nobody awaits it) -- resuming null segfaults.
    // SuspendAndDestroy frees the frame and returns to the worker instead.
    static Coroutine::Task wait_group_runner(Coroutine::Task task) {
        co_await std::move(task);
        WaitGroup::done();
        co_await Coroutine::SuspendAndDestroy{};
    }

    void wait_group_spawn(Coroutine::Task task) {
        WaitGroup::add();
        Coroutine::Task runner = wait_group_runner(std::move(task));
        global_work_queue_schedule(std::move(runner)._into_handle());
    }

}

namespace wry::execution {
    
    /*
    define_test("coroutine") {
        []() -> co_future<int> {
            co_await []() -> co_future<double> {
                co_return 8.0;
            }();
            co_return 7;
        }();
        
        Flow flow;
        
        flow.fork([]() -> co_future<int> { co_return 7; }());
        
        co_return;
    };
     */
    
    define_test("co_sender") {
        
        auto a = []() -> co_sender<int> {
            printf("co_sender<int>\n");
            auto b = []() -> co_sender<int> {
                printf("co_sender<int2>\n");
                co_return 7;
            }();
            co_return co_await b;
        }();
        
        auto b = []() -> co_sender<> {
            printf("co_sender<>\n");
            co_return;
        }();
        
        a.connect(execution::_trivial_receiver{}).start();
        b.connect(execution::_trivial_receiver{}).start();

        
        co_return;
    };
    
}
