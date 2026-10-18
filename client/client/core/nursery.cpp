//
//  nursery.cpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#include <atomic>
#include <cassert>
#include <optional>
#include <stop_token>

#include "nursery.hpp"
#include "coroutine_test.hpp"
#include "test.hpp"

namespace wry::coroutine {

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

    define_test("nursery_unwind") {

        std::atomic<int> reached{0};
        std::atomic<int> destroyed{0};
        execution::Outcome<std::optional<int>> target{};
        execution::Outcome<int> ok_result;

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
        assert(target.is_stopped());
        [[maybe_unused]] int ok = co_await ok_result;
        assert(ok == 7);

        // A nursery that absorbed a cancellation is not reusable
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
            Nursery nursery{co_await GetStopToken{}};
            nursery.soon(unwind_scope_child(reached, destroyed));
            co_await nursery.join();
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("nursery_scope_cancel") {

        // Scope-cancelled propagation: the host's stop token is requested up
        // front; GetStopToken hands it to the nursery; when the last child retires
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
        fence._stop_token = source.get_token();
        task._promise->set_delegate(&fence);
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
            Nursery nursery{co_await GetStopToken{}};
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
            Nursery nursery{co_await GetStopToken{}};
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
            Nursery nursery{co_await GetStopToken{}};
            nursery.soon(unwind_token_watching_child(started, destroyed));
            nursery.soon(unwind_token_watching_child(started, destroyed));
            co_await nursery.join();
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("nursery_scope_cancel_empty") {

        // A stop-requested scope with no children must still unwind at
        // join(): await_ready refuses the fast path and await_suspend's
        // zero branch destroys the joiner in place.
        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> destroyed{0};      // host only
        std::atomic<int> resumed_past{0};

        source.request_stop();
        Task task = unwind_empty_host(&destroyed, &resumed_past);
        fence._stop_token = source.get_token();
        task._promise->set_delegate(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);

        co_return;
    };

    define_test("nursery_interior_cancel") {

        // nursery.request_stop() reaches the children through their inner
        // tokens; both cancel; the joiner resumes normally (outer token
        // unrequested) and join reports the tally.  The host then completes
        // through word 0.
        UnwindFence fence;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};      // two children + host
        std::atomic<std::ptrdiff_t> tally{-1};

        Task task = unwind_interior_host(&started, &destroyed, &tally);
        task._promise->set_delegate(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(fence._outcome.load(std::memory_order_relaxed) == 1);
        assert(started.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 3);
        assert(tally.load(std::memory_order_relaxed) == 2);

        co_return;
    };

    define_test("nursery_outer_bridge") {

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
        fence._stop_token = source.get_token();
        task._promise->set_delegate(&fence);
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


    // ---- Sender / receiver seam ---------------------------------------------

    // Routes a sender through the generic path even when it is also
    // awaitable (a Future would otherwise take its direct path)
    template<execution::Sender S>
    struct ViaSender {
        S _inner;
    };

    namespace {

        // A child's token is its nursery's interior token, reached through
        // the delegate
        Future<int> value_child(int value) {
            co_await TransferToPoolExecutor{};
            co_return value;
        }

        Task scope_token_probe(Nursery* scope, std::atomic<int>* result) {
            std::stop_token token = co_await GetStopToken{};
            bool is_inner = (token == scope->_inner_stop_source.get_token()) && token.stop_possible();
            result->store(is_inner ? 1 : 2, std::memory_order_release);
        }

    }

    define_test("nursery_scope_stop_token") {
        // The runner is shielded from the process scope's token (main.mm),
        // so this frame's own token is detached; a child spawned into a
        // nursery sees that nursery's interior token, and a nursery given a
        // token bridges rather than shares it
        std::stop_token mine = co_await GetStopToken{};
        assert(!mine.stop_possible());
        std::atomic<int> result{0};
        Nursery scope;
        scope.soon(scope_token_probe(&scope, &result));
        [[maybe_unused]] std::ptrdiff_t cancelled = co_await scope.join();
        assert(cancelled == 0);
        assert(result.load(std::memory_order_acquire) == 1);
        std::stop_token outer = scope._inner_stop_source.get_token();
        Nursery nursery{outer};
        assert(nursery._outer_stop_token == outer);
        assert(nursery._inner_stop_source.get_token() != outer);
        cancelled = co_await nursery.join();
        assert(cancelled == 0);
        co_return;
    };

    define_test("nursery_value_target") {
        // A bald target: the value is assigned through, with no Outcome per
        // child and no await per result.  An exception or a stop on such a
        // child aborts; these have neither
        int results[3] = {};
        Nursery nursery;
        nursery.soon(results[0], value_child(1));
        co_await nursery.fork(results[1], value_child(2));
        co_await nursery.fork(results[2], value_child(14));
        co_await nursery.join();
        assert((results[0] == 1) && (results[1] == 2) && (results[2] == 14));
        co_return;
    };

} // namespace wry::coroutine
