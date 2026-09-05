//
//  coroutine.cpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <stop_token>

#include <dispatch/dispatch.h>

#include "coroutine.hpp"

#include "kqueue_reactor.hpp"
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

        void wait_group_retire(void*) {
            std::ptrdiff_t n = g_wait_group_count.sub_fetch_release(1);
            assert(n >= 0);
            if (n == 0) { std::atomic_thread_fence(std::memory_order::acquire); tsan_acquire(&g_wait_group_count);
                Coroutine::resume_by_address(g_wait_group_callback); }
        }
        constinit Coroutine::Header g_wait_group_sentinel = {
            &wait_group_retire,
            &wait_group_retire
        };

        void (*g_wait_group_notify_all)(void*) = [](void*) {
            g_wait_group_count.notify_all();
        };

    }

    void wait_group_spawn(Coroutine::Task task) {
        std::ptrdiff_t observed = g_wait_group_count.fetch_add_relaxed(1);
        assert(observed && "wait_group_spawn after wait_group_wait");
        task._promise->set_continuation(&g_wait_group_sentinel);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        assert(task._promise == nullptr);
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
            Coroutine::resume_by_address(g_wait_group_callback);
        }
    }

}

namespace wry::Coroutine {

    void TransferToBlockableExecutor::await_suspend(std::coroutine_handle<> handle) const noexcept {
        dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                         handle.address(),
                         &resume_by_address);
    }

    // Policy: We use libdispatch to implement waiting, but on waking we send
    // the actual work back to the global work queue

    void global_work_queue_schedule_after(std::chrono::steady_clock::time_point when, void* address) {
        auto now = std::chrono::steady_clock::now();
        int64_t ns = (when > now)
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(when - now).count()
        : 0;
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, ns),
                         dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                         address,
                         &global_work_queue_schedule);
    }

    // Unwinding exercise.  A cancelled leaf destroys its own frame; each
    // ~Promise in the continuation chain then destroys its caller's frame in
    // turn, running that frame's local destructors, until the chain bottoms
    // out at a two-word fake frame (completion arrives through word 0,
    // cancellation through word 1).
    //
    // The fence below is such a fake frame, standing in for the scope so the
    // cascade never reaches the test's own frame.  The whole cascade runs
    // synchronously on the thread that resumed the leaf; the fence store is a
    // release paired with the polling acquire, so the probe counts written
    // during the unwind are visible to the asserts.

    namespace {

        struct UnwindFence {
            void (*_resume)(void*) = &_static_resume;
            void (*_destroy)(void*) = &_static_destroy;
            std::atomic<int> _outcome{0};  // 0 pending, 1 completed, 2 cancelled
            static void _static_resume(void* ptr) {
                ((UnwindFence*)ptr)->_outcome.store(1, std::memory_order_release);
            }
            static void _static_destroy(void* ptr) {
                ((UnwindFence*)ptr)->_outcome.store(2, std::memory_order_release);
            }
        };

        // Frame-local sentinel: its destructor must run exactly once whether
        // the frame completes or is unwound.
        struct UnwindProbe {
            std::atomic<int>* _counter;
            ~UnwindProbe() { _counter->fetch_add(1, std::memory_order_relaxed); }
        };

        Task unwind_completing_task(std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            co_await TransferToPoolExecutor{};
            co_return;
        }

        Task unwind_leaf(std::atomic<int>* reached, std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            reached->fetch_add(1, std::memory_order_relaxed);
            co_await SuspendAndCancel{};
            // unreachable: SuspendAndCancel never resumes
        }

        Future<int> unwind_mid(std::atomic<int>* reached,
                               std::atomic<int>* destroyed,
                               std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            co_await unwind_leaf(reached, destroyed);
            resumed_past->fetch_add(1, std::memory_order_relaxed);
            co_return 7;
        }

        Task unwind_host(std::atomic<int>* reached,
                         std::atomic<int>* destroyed,
                         std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            int x = co_await unwind_mid(reached, destroyed, resumed_past);
            (void) x;
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("coroutine_unwind_chain") {

        // Normal completion delivers through word 0, exactly once: the
        // FinalAwaitable's exchange must leave ~Promise nothing to destroy,
        // or the fence would also see word 1.
        {
            UnwindFence fence;
            std::atomic<int> destroyed{0};
            Task task = unwind_completing_task(&destroyed);
            task._promise->set_continuation(&fence);
            global_work_queue_schedule(handle_from_future(std::move(task)));
            while (fence._outcome.load(std::memory_order_acquire) == 0)
                co_await TransferToPoolExecutor{};
            assert(fence._outcome.load(std::memory_order_relaxed) == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
        }

        // Cancellation: the leaf destroys itself, the cascade destroys mid
        // and host (their probes fire exactly once), no code after any
        // co_await runs, and the fence sees word 1 exactly once.
        {
            UnwindFence fence;
            std::atomic<int> reached{0};
            std::atomic<int> destroyed{0};      // leaf + mid + host
            std::atomic<int> resumed_past{0};   // must stay zero
            Task task = unwind_host(&reached, &destroyed, &resumed_past);
            task._promise->set_continuation(&fence);
            global_work_queue_schedule(handle_from_future(std::move(task)));
            while (fence._outcome.load(std::memory_order_acquire) == 0)
                co_await TransferToPoolExecutor{};
            assert(fence._outcome.load(std::memory_order_relaxed) == 2);
            assert(reached.load(std::memory_order_relaxed) == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 3);
            assert(resumed_past.load(std::memory_order_relaxed) == 0);
        }

        co_return;
    };

    namespace {

        Future<std::optional<int>>
        unwind_cancelled_child(std::atomic<int>* reached,
                               std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            reached->fetch_add(1, std::memory_order_relaxed);
            co_await SuspendAndCancel{};
            co_return 0;  // unreachable
        }

        Future<int> unwind_ok_child(int value) {
            co_await TransferToPoolExecutor{};
            co_return value;
        }

    }

    define_test("coroutine_unwind_nursery") {

        std::atomic<int> reached{0};
        std::atomic<int> destroyed{0};
        std::optional<int> target{};  // cancelled child must never write this
        int ok_result = 0;

        // A child's self-cancellation is absorbed in any retire order: the
        // outer token is unrequested, so the policy always resumes the
        // joiner, and join reifies the tally.
        Nursery nursery;
        nursery.soon(target, unwind_cancelled_child(&reached, &destroyed));
        nursery.soon(ok_result, unwind_ok_child(7));
        std::ptrdiff_t cancelled = co_await nursery.join();

        assert(cancelled == 1);
        assert(reached.load(std::memory_order_relaxed) == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(!target.has_value());
        assert(ok_result == 7);

        // A nursery that absorbed a cancellation is reusable, and the tally
        // was reset by the previous join
        nursery.soon(ok_result, unwind_ok_child(42));
        cancelled = co_await nursery.join();
        assert(cancelled == 0);
        assert(ok_result == 42);

        co_return;
    };

    namespace {

        Task unwind_scope_child(std::atomic<int>* reached,
                                std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            reached->fetch_add(1, std::memory_order_relaxed);
            // No ordering gate: whether this child retires before or after
            // the joiner arms, every join path now consults the outer token
            // (await_ready refuses the fast path, await_suspend's zero branch
            // and the retire path both unwind), so any interleaving must
            // reach the fence through word 1.
            co_await SuspendAndCancel{};
        }

        Task unwind_scope_host(std::atomic<int>* reached,
                               std::atomic<int>* destroyed,
                               std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            Nursery nursery = co_await Nursery::Factory{};
            nursery.soon(unwind_scope_child(reached, destroyed));
            co_await nursery.join();
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("coroutine_unwind_scope_cancel") {

        // Scope-cancelled propagation: the host's stop token is requested up
        // front; Factory hands it to the nursery; when the last child retires
        // the armed joiner is unwound through word 1 instead of resumed, and
        // the cascade runs the host's frame-local destructors on its way to
        // the fence.
        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> reached{0};
        std::atomic<int> destroyed{0};      // child + host
        std::atomic<int> resumed_past{0};   // the join must never resume

        source.request_stop();
        Task task = unwind_scope_host(&reached, &destroyed, &resumed_past);
        task._promise->set_stop_token(source.get_token());
        task._promise->set_continuation(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 2);
        assert(reached.load(std::memory_order_relaxed) == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 2);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);

        co_return;
    };

    namespace {

        Task unwind_empty_host(std::atomic<int>* destroyed,
                               std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            Nursery nursery = co_await Nursery::Factory{};
            co_await nursery.join();
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

        // Starts, then waits on its own (inner) token, then cancels itself.
        Task unwind_token_watching_child(std::atomic<int>* started,
                                         std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::stop_token token = co_await GetStopToken{};
            while (!token.stop_requested())
                co_await TransferToPoolExecutor{};
            co_await SuspendAndCancel{};
        }

        Task unwind_interior_host(std::atomic<int>* started,
                                  std::atomic<int>* destroyed,
                                  std::atomic<std::ptrdiff_t>* tally) {
            UnwindProbe probe{destroyed};
            Nursery nursery = co_await Nursery::Factory{};
            nursery.soon(unwind_token_watching_child(started, destroyed));
            nursery.soon(unwind_token_watching_child(started, destroyed));
            // Scope-internal cancellation: hastens the children, but the
            // outer token is unrequested, so the join resumes normally and
            // reports.
            nursery.request_stop();
            tally->store(co_await nursery.join(), std::memory_order_relaxed);
        }

        Task unwind_bridge_host(std::atomic<int>* started,
                                std::atomic<int>* destroyed,
                                std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            Nursery nursery = co_await Nursery::Factory{};
            nursery.soon(unwind_token_watching_child(started, destroyed));
            nursery.soon(unwind_token_watching_child(started, destroyed));
            co_await nursery.join();
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("coroutine_unwind_scope_cancel_empty") {

        // A stop-requested scope with no children must still unwind at
        // join(): await_ready refuses the fast path and await_suspend's
        // zero branch destroys the joiner in place.
        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> destroyed{0};      // host only
        std::atomic<int> resumed_past{0};

        source.request_stop();
        Task task = unwind_empty_host(&destroyed, &resumed_past);
        task._promise->set_stop_token(source.get_token());
        task._promise->set_continuation(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);

        co_return;
    };

    define_test("coroutine_unwind_interior_cancel") {

        // nursery.request_stop() reaches the children through their inner
        // tokens; both cancel; the joiner resumes normally (outer token
        // unrequested) and join reports the tally.  The host then completes
        // through word 0.
        UnwindFence fence;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};      // two children + host
        std::atomic<std::ptrdiff_t> tally{-1};

        Task task = unwind_interior_host(&started, &destroyed, &tally);
        task._promise->set_continuation(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 1);
        assert(started.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 3);
        assert(tally.load(std::memory_order_relaxed) == 2);

        co_return;
    };

    namespace {

        Future<int> osfs_value_child() {
            co_await TransferToPoolExecutor{};
            co_return 17;
        }

        Future<int> osfs_throwing_child() {
            co_await TransferToPoolExecutor{};
            throw std::runtime_error("osfs");
        }

        Future<int> osfs_stoppable_child() {
            std::stop_token token = co_await GetStopToken{};
            // Bounded, so a broken token chain fails the test instead of
            // hanging it: if the stop request never becomes visible here,
            // fall through and complete with a sentinel the test rejects.
            for (int i = 0; i != (1 << 16); ++i) {
                if (token.stop_requested())
                    co_await SuspendAndCancel{};
                co_await TransferToPoolExecutor{};
            }
            co_return -1;
        }

        Task osfs_host(std::atomic<int>* value,
                       std::atomic<int>* caught,
                       std::atomic<int>* stopped_ok) {
            // Value channel passes through
            std::optional<int> a = co_await stopped_as_optional(osfs_value_child());
            if (a && (*a == 17))
                value->store(17, std::memory_order_relaxed);

            // Exception channel passes through
            try {
                (void) co_await stopped_as_optional(osfs_throwing_child());
            } catch (std::runtime_error const&) {
                caught->fetch_add(1, std::memory_order_relaxed);
            }

            // Stopped channel becomes nullopt: the child observes the
            // requested token through the adaptor, cancels itself, the
            // cascade is absorbed at the adaptor's destroy word, and THIS
            // frame is resumed rather than destroyed
            std::optional<int> c = co_await stopped_as_optional(osfs_stoppable_child());
            if (!c)
                stopped_ok->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("coroutine_optional_from_stopped") {

        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> value{0};
        std::atomic<int> caught{0};
        std::atomic<int> stopped_ok{0};

        // The scope is cancelled from the start: children that do not look
        // at the token still complete on their own channels; the one that
        // looks must complete stopped.
        source.request_stop();
        Task task = osfs_host(&value, &caught, &stopped_ok);
        task._promise->set_stop_token(source.get_token());
        task._promise->set_continuation(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};

        // The host absorbed a child cancellation and still completed
        // normally through word 0
        assert(fence._outcome.load(std::memory_order_relaxed) == 1);
        assert(value.load(std::memory_order_relaxed) == 17);
        assert(caught.load(std::memory_order_relaxed) == 1);
        assert(stopped_ok.load(std::memory_order_relaxed) == 1);

        co_return;
    };

    define_test("coroutine_unwind_outer_bridge") {

        // A live outer request (after the children are demonstrably running)
        // forwards through the nursery's stop_callback into the inner token:
        // the children hasten and cancel, and at retire-zero the requested
        // outer token unwinds the joiner through word 1.
        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};      // two children + host
        std::atomic<int> resumed_past{0};

        Task task = unwind_bridge_host(&started, &destroyed, &resumed_past);
        task._promise->set_stop_token(source.get_token());
        task._promise->set_continuation(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (started.load(std::memory_order_acquire) != 2)
            co_await TransferToPoolExecutor{};
        source.request_stop();
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 3);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);

        co_return;
    };

}

namespace wry::execution {

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
