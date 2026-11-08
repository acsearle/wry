//
//  coroutine.cpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>

#include <dispatch/dispatch.h>

#include "coroutine.hpp"
#include "coroutine_test.hpp"
#include "kqueue_reactor.hpp"
#include "test.hpp"

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

    namespace {

        // Native stack extent seen by a coroutine body across many awaits of
        // senders that complete inside their start.  __builtin_frame_address
        // in a coroutine body is the native frame of the resume function the
        // body is running in, so if each await nests a resume inside the
        // previous one, the frame address walks down the stack once per
        // iteration
        struct StackExtent {
            std::uintptr_t lo = UINTPTR_MAX;
            std::uintptr_t hi = 0;
            void sample(void* frame) {
                std::uintptr_t a = (std::uintptr_t)frame;
                lo = std::min(lo, a);
                hi = std::max(hi, a);
            }
            std::uintptr_t spread() const { return hi - lo; }
        };

        Future<int> sr_inline_loop(int n, StackExtent* extent) {
            int sum = 0;
            for (int i = 0; i != n; ++i) {
                sum += co_await execution::just(1);
                extent->sample(__builtin_frame_address(0));
                auto r = co_await execution::when_any(execution::just(1), execution::just(2));
                sum += (r.index == 0) && (r.value == 1);
                extent->sample(__builtin_frame_address(0));
                auto o = co_await execution::stopped_as_optional(execution::just(1));
                sum += *o;
                extent->sample(__builtin_frame_address(0));
            }
            co_return sum;
        }

    }

    define_test("coroutine_sender_inline_stack") {

        // A loop over senders that complete inline (Just, a race of Justs,
        // an adaptor over a Just) must run in constant native stack: each
        // completion inside start records the outcome, and await_suspend
        // transfers back to the awaiting frame instead of the receiver
        // resuming it from inside start.  Were the receiver to resume
        // inline, every iteration would nest three resumes and the spread
        // would grow linearly in n
        constexpr int n = 256;
        execution::Outcome<int> outcome;
        std::atomic<int> done{0};
        StackExtent extent;
        auto op = connect(sr_inline_loop(n, &extent), OutcomeReceiver<int>{&outcome, &done, {}});
        start(op);
        assert(done.load(std::memory_order_acquire) == 1);
        assert(outcome._variant.index() == 1);
        assert(std::get<1>(outcome._variant) == 3 * n);
        printf("coroutine_sender_inline_stack: %d awaits, native stack spread %zu bytes\n",
               3 * n, (std::size_t) extent.spread());
        assert(extent.spread() < 4096);

        co_return;
    };

    // A sender that completes with value or stopped, either inside start
    // (inline) or on a pool worker that start schedules, after which start
    // idles for a while without touching the operation: the worker's
    // completion then races start's return, and so the awaiting thread's
    // exchange, in either order
    struct ArrivalSender {
        int _value;
        bool _stop;
        bool _inline;
        int _spin;
    };

} namespace wry::execution {
    template<>
    struct SenderTraits<coroutine::ArrivalSender> {
        using value_type = int;
    };
} namespace wry::coroutine {

    template<typename R>
    struct ArrivalOperationState {
        // A two-word thunk for the pool, as sync_wait's Starter
        void (*_resume)(void*) = &_static_resume;
        void (*_destroy)(void*) = &_static_destroy;
        ArrivalSender _sender;
        R _receiver;
        static void _static_resume(void* ptr) {
            auto self = (ArrivalOperationState*)ptr;
            // last touch of self on every path
            if (self->_sender._stop) {
                set_stopped(std::move(self->_receiver));
            } else {
                int value = self->_sender._value;
                set_value(std::move(self->_receiver), std::move(value));
            }
        }
        static void _static_destroy(void*) {
            abort();
        }
        template<typename R2>
        ArrivalOperationState(ArrivalSender sender, R2&& receiver)
        : _sender(sender), _receiver(std::forward<R2>(receiver)) {
        }
        ArrivalOperationState(ArrivalOperationState const&) = delete;
    };

    template<typename R>
    auto connect(ArrivalSender&& sender, R&& receiver) {
        return ArrivalOperationState<std::remove_cvref_t<R>>(sender, std::forward<R>(receiver));
    }

    template<typename R>
    void start(ArrivalOperationState<R>& op) {
        if (op._sender._inline)
            return ArrivalOperationState<R>::_static_resume(&op);  // last touch
        int spin = op._sender._spin;
        global_work_queue_schedule((void*)&op);  // last touch of op
        for (volatile int k = 0; k < spin; k = k + 1)
            ;
    }

    namespace {

        Future<int> sr_arrival_loop(int n) {
            int sum = 0;
            for (int i = 0; i != n; ++i)
                sum += co_await ArrivalSender{1, false, (i % 3) == 0, (i * 37) % 2000};
            co_return sum;
        }

        Future<int> sr_arrival_stopped(bool is_inline, int spin, std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            (void) co_await ArrivalSender{0, true, is_inline, spin};
            abort();  // a stopped completion destroys this frame
            co_return 0;
        }

    }

    define_test("coroutine_sender_arrival") {

        // Value completions inline and on a worker, the worker's racing the
        // return of start: every await continues exactly once, with the
        // value the completion recorded
        {
            constexpr int n = 4000;
            execution::Outcome<int> outcome;
            std::atomic<int> done{0};
            auto op = connect(sr_arrival_loop(n), OutcomeReceiver<int>{&outcome, &done, {}});
            start(op);
            while (done.load(std::memory_order_acquire) == 0)
                co_await TransferToPoolExecutor{};
            assert(outcome._variant.index() == 1);
            assert(std::get<1>(outcome._variant) == n);
        }

        // Stopped completions: whichever party arrives second destroys the
        // awaiting frame, exactly once, and the cancellation reaches the
        // terminal receiver
        for (int i = 0; i != 400; ++i) {
            execution::Outcome<int> outcome;
            std::atomic<int> done{0}, destroyed{0};
            auto op = connect(sr_arrival_stopped((i % 4) == 0, (i * 37) % 2000, &destroyed),
                              OutcomeReceiver<int>{&outcome, &done, {}});
            start(op);
            while (done.load(std::memory_order_acquire) == 0)
                co_await TransferToPoolExecutor{};
            assert(outcome._variant.index() == 3);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
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
