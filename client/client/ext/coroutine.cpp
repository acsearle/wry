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

#include <dispatch/dispatch.h>

#include "coroutine.hpp"

#include "execution.hpp"
#include "test.hpp"

#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#endif

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<> handle) {
        global_work_queue_schedule(handle.address());
    }

    namespace {

        // g_wait_group_callback is published by the waiter (a plain write) and
        // read by whichever worker drives g_wait_group_count to zero.  The
        // synchronization is real and C++20-correct: the waiter's release on
        // g_wait_group_count (after writing the callback) is picked up by the
        // worker's acquire fence, via the release sequence that runs through
        // the workers' sub_fetch RMWs.  ThreadSanitizer does not reconstruct
        // happens-before from a standalone acquire fence over a release
        // sequence, so it false-positives on the non-atomic callback word.
        // tsan_release / tsan_acquire on the callback's address hand TSan the
        // edge it cannot derive (cf. kqueue_reactor, where the kernel hides
        // the same edge).  No-ops outside a TSan build.
#if __has_feature(thread_sanitizer)
        void tsan_acquire(void* addr) { __tsan_acquire(addr); }
#else
        void tsan_acquire(void*) {}
#endif

        // Initial count includes sentinel
        constinit wry::Atomic<std::ptrdiff_t> g_wait_group_count{1};

        // Not atomic; protected by sentinel
        void* g_wait_group_callback = nullptr;

        void (*g_wait_group_continuation)(void*) = [](void*) {
            std::ptrdiff_t n = g_wait_group_count.sub_fetch_release(1);
            assert(n >= 0);
            if (n == 0) {
                std::atomic_thread_fence(std::memory_order::acquire);
                tsan_acquire(&g_wait_group_count);
                (*((void(**)(void*))g_wait_group_callback))(g_wait_group_callback);
            }
        };

        void (*g_wait_group_notify_all)(void*) = [](void*) {
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
        g_wait_group_callback = &g_wait_group_notify_all;
        std::ptrdiff_t expected = g_wait_group_count.sub_fetch_release(1);
        assert(expected >= 0);
        while (expected) {
            g_wait_group_count.wait(expected, Ordering::RELAXED);
            assert(expected >= 0);
        }
        std::atomic_thread_fence(std::memory_order::acquire);
        tsan_acquire(&g_wait_group_count);
    }

    void wait_group_set_callback(void* callback) {
        g_wait_group_callback = callback;
        std::ptrdiff_t expected = g_wait_group_count.sub_fetch_release(1);
        assert(expected >= 0);
        if (expected == 0) {
            // If all the tasks were finished, invoke immediately
            std::atomic_thread_fence(std::memory_order_acquire);
            tsan_acquire(&g_wait_group_count);
            (*((void(**)(void*))g_wait_group_callback))(g_wait_group_callback);
        }
    }

}

namespace wry::Coroutine {

    namespace {
        // libdispatch invokes this with the suspended coroutine handle's address
        // as its context argument; reconstruct the handle and resume.  The
        // coroutine then runs on the dispatch worker thread until its next
        // suspension or completion.
        void resume_coroutine_from_dispatch(void* context) noexcept {
            std::coroutine_handle<>::from_address(context).resume();
        }
    }

    void Until::await_suspend(std::coroutine_handle<> handle) const noexcept {
        // await_ready already handled the already-past case, but clamp anyway so
        // a deadline that slipped between the two calls schedules at +0 rather
        // than wrapping negative.
        auto now = std::chrono::steady_clock::now();
        int64_t ns = (_when > now)
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(_when - now).count()
            : 0;
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, ns),
                         dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                         handle.address(),
                         &resume_coroutine_from_dispatch);
    }

    void ScheduleOnBlockableThread::await_suspend(std::coroutine_handle<> handle) const noexcept {
        dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                         handle.address(),
                         &resume_coroutine_from_dispatch);
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
