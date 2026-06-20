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

    namespace {

        constinit wry::Atomic<std::ptrdiff_t> g_wait_group_count{1};

        void (*g_wait_group_continuation)(void*) = [](void*) {
            std::ptrdiff_t n = g_wait_group_count.sub_fetch_release(1);
            assert(n >= 0);
            if (n == 0)
                g_wait_group_count.notify_all();
        };

    }

    void wait_group_spawn(Coroutine::Task task) {
        std::ptrdiff_t observed = g_wait_group_count.fetch_add_relaxed(1);
        assert(observed && "wait_group_spawn after wait_group_wait");
        task._set_continuation(&g_wait_group_continuation);
        global_work_queue_schedule(std::move(task)._into_handle());
    }

    void wait_group_wait() {
        std::ptrdiff_t expected = g_wait_group_count.sub_fetch_relaxed(1);
        while (expected)
            g_wait_group_count.wait(expected, Ordering::RELAXED);
        std::atomic_thread_fence(std::memory_order_acquire);
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
