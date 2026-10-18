//
//  coroutine.cpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>

#include <dispatch/dispatch.h>

#include "coroutine.hpp"
#include "coroutine_test.hpp"
#include "kqueue_reactor.hpp"
#include "test.hpp"

#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#endif

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<> handle) {
        global_work_queue_schedule(handle.address());
    }

    inline void global_work_queue_schedule_destroy_by_address(void* ptr) {
        struct DestroyThunk {
            coroutine::Header _header = { &_static_destroy, &_static_destroy };
            void* continuation = nullptr;
            static void _static_destroy(void* ptr) {
                auto self = (DestroyThunk*)ptr;
                void* continuation = self->continuation;
                delete self;
                [[clang::musttail]] return coroutine::destroy_by_address(continuation);
            }
        };
        global_work_queue_schedule(new DestroyThunk{ .continuation = ptr });
    }



}

namespace wry::coroutine {

    void TransferToBlockableExecutor::await_suspend(std::coroutine_handle<> handle) const noexcept {
        dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                         handle.address(),
                         &resume_by_address);
    }

#pragma - Testing

    // The unwinding cascade itself (the fence and probe are in
    // coroutine_test.hpp): a chain of three frames unwound from the leaf.

    namespace {

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
            task._promise->set_delegate(&fence);
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
            task._promise->set_delegate(&fence);
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

    // ---- Sender / receiver seam ---------------------------------------------

    // Routes a sender through the generic path even when it is also
    // awaitable (a Future would otherwise take its direct path)
    template<execution::Sender S>
    struct ViaSender {
        S _inner;
    };

} namespace wry::execution {
    template<execution::Sender S>
    struct SenderTraits<coroutine::ViaSender<S>> {
        using value_type = SenderValueType<S>;
    };
} namespace wry::coroutine {

    template<execution::Sender S, typename R>
    auto connect(ViaSender<S>&& via, R&& receiver) {
        return connect(std::move(via._inner), std::forward<R>(receiver));
    }

    namespace {

        // Records the outcome into a cell and flags completion; the flag's
        // release / acquire is the publishing edge for a completion that
        // arrives on another worker
        template<typename T>
        struct OutcomeReceiver {
            execution::Outcome<T>* _outcome;
            std::atomic<int>* _done;
            std::stop_token _stop_token;
        };

        template<typename T>
        void set_value(OutcomeReceiver<T>&& receiver, T&& value) {
            set_value(*receiver._outcome, std::move(value));
            receiver._done->store(1, std::memory_order_release);
        }

        void set_value(OutcomeReceiver<void>&& receiver) {
            set_value(*receiver._outcome);
            receiver._done->store(1, std::memory_order_release);
        }

        template<typename T>
        void set_error(OutcomeReceiver<T>&& receiver, std::exception_ptr&& error) {
            set_error(*receiver._outcome, std::move(error));
            receiver._done->store(1, std::memory_order_release);
        }

        template<typename T>
        void set_stopped(OutcomeReceiver<T>&& receiver) {
            set_stopped(*receiver._outcome);
            receiver._done->store(1, std::memory_order_release);
        }

        template<typename T>
        std::stop_token get_stop_token(OutcomeReceiver<T> const& receiver) {
            return receiver._stop_token;
        }

        Future<int> sr_one() {
            co_return 1;
        }

        Future<int> sr_add(int x) {
            int y = co_await execution::just(2);          // generic path, synchronous
            int z = co_await sr_one();              // direct path
            int w = co_await ViaSender{sr_one()};   // generic path, Future as sender
            co_return x + y + z + w;
        }

        Future<int> sr_thrower() {
            throw std::runtime_error("boom");
            co_return 0;
        }

        Future<int> sr_catcher() {
            try {
                (void) co_await ViaSender{sr_thrower()};
            } catch (std::runtime_error const&) {
                co_return 42;
            }
            co_return 0;
        }

        Task sr_void_child(std::atomic<int>* ran) {
            ran->fetch_add(1, std::memory_order_relaxed);
            co_return;
        }

        // A real leaf: parks on the reactor
        Future<int> sr_leaf(double seconds, std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            co_await cancelable_sleep(seconds);
            co_return 7;
        }

        Future<int> sr_trunk(double seconds,
                             std::atomic<int>* started,
                             std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            int v = co_await ViaSender{sr_leaf(seconds, destroyed)};
            co_return v;
        }

    }

    define_test("coroutine_sender_seam") {

        // value: Just (synchronous, generic path), the direct path, and a
        // Future routed through the generic sender path; nothing suspends
        // for real, so the whole thing completes inside start
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0};
            auto op = connect(sr_add(10), OutcomeReceiver<int>{&outcome, &done, {}});
            start(op);
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 1);
            assert(std::get<1>(outcome._variant) == 14);
        }

        // error crossing two seams and caught in the middle
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0};
            auto op = connect(sr_catcher(), OutcomeReceiver<int>{&outcome, &done, {}});
            start(op);
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 1);
            assert(std::get<1>(outcome._variant) == 42);
        }

        // error reaching the terminal receiver
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0};
            auto op = connect(sr_thrower(), OutcomeReceiver<int>{&outcome, &done, {}});
            start(op);
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 2);
            bool caught = false;
            try {
                std::rethrow_exception(std::get<2>(outcome._variant));
            } catch (std::runtime_error const& e) {
                caught = (std::string_view(e.what()) == "boom");
            }
            assert(caught);
        }

        // a Task (void) as a sender
        {
            execution::Outcome<void> outcome;
            std::atomic<int> done{0};
            std::atomic<int> ran{0};
            auto op = connect(sr_void_child(&ran), OutcomeReceiver<void>{&outcome, &done, {}});
            start(op);
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 1);
            assert(ran.load(std::memory_order_relaxed) == 1);
        }

        co_return;
    };

    define_test("coroutine_sender_seam_cancel") {

        using namespace std::chrono;

        // Parked cancel: the trunk awaits its leaf through the generic sender
        // path and the leaf parks on the reactor.  Requesting the terminal
        // receiver's token reaches the leaf through both seams, and the
        // destroy cascade comes back through both -- synchronously, inside
        // request_stop
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0}, started{0}, destroyed{0};
            std::stop_source source;
            auto op = connect(sr_trunk(10.0, &started, &destroyed),
                              OutcomeReceiver<int>{&outcome, &done, source.get_token()});
            start(op);  // runs the trunk inline until the leaf parks
            assert(started.load(std::memory_order_acquire) == 1);
            co_await cancelable_sleep(0.010);  // settle past the arming window
            auto t0 = steady_clock::now();
            source.request_stop();
            auto elapsed = steady_clock::now() - t0;
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 3);
            assert(destroyed.load(std::memory_order_relaxed) == 2);
            assert(elapsed < seconds(2));
        }

        // Cancel before park: the token is already requested when the leaf
        // reaches its cancellation point, so the whole tower is torn down
        // inside start
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0}, started{0}, destroyed{0};
            std::stop_source source;
            source.request_stop();
            auto op = connect(sr_trunk(10.0, &started, &destroyed),
                              OutcomeReceiver<int>{&outcome, &done, source.get_token()});
            start(op);
            assert(done.load(std::memory_order_acquire) == 1);
            assert(outcome._variant.index() == 3);
            assert(started.load(std::memory_order_relaxed) == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 2);
        }

        // Not stopped: the same tower completes normally on a worker after
        // the leaf's timer fires
        {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0}, started{0}, destroyed{0};
            std::stop_source source;
            auto op = connect(sr_trunk(0.0, &started, &destroyed),
                              OutcomeReceiver<int>{&outcome, &done, source.get_token()});
            start(op);
            while (done.load(std::memory_order_acquire) == 0)
                co_await TransferToPoolExecutor{};
            assert(outcome._variant.index() == 1);
            assert(std::get<1>(outcome._variant) == 7);
            assert(destroyed.load(std::memory_order_relaxed) == 2);
        }

        co_return;
    };



}
