//
//  execution.cpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#include <atomic>
#include <cassert>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <stop_token>

#include "execution.hpp"
#include "coroutine.hpp"
#include "coroutine_test.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "test.hpp"

// Written in the coroutine namespace: the senders under test are Futures,
// and the hosts are tasks
namespace wry::coroutine {

    namespace {

        // sync_wait's senders: a value through the generic path, an error,
        // and a void task
        Future<int> sw_add(int x) {
            int y = co_await execution::just(4);
            co_return x + y;
        }

        Future<int> sw_thrower() {
            throw std::runtime_error("boom");
            co_return 0;
        }

        Task sw_void_child(std::atomic<int>* ran) {
            ran->fetch_add(1, std::memory_order_relaxed);
            co_return;
        }

    }

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

        Future<int> osfs_shielded_child() {
            std::stop_token token = co_await GetStopToken{};
            // shield hands the inner a detached token: nobody can request it
            co_return token.stop_possible() ? -1 : 5;
        }

        Task osfs_host(std::atomic<int>* value,
                       std::atomic<int>* caught,
                       std::atomic<int>* stopped_ok,
                       std::atomic<int>* shielded,
                       std::atomic<int>* budgeted) {
            // Value channel passes through
            std::optional<int> a = co_await execution::stopped_as_optional(osfs_value_child());
            if (a && (*a == 17))
                value->store(17, std::memory_order_relaxed);

            // Exception channel passes through
            try {
                (void) co_await execution::stopped_as_optional(osfs_throwing_child());
            } catch (std::runtime_error const&) {
                caught->fetch_add(1, std::memory_order_relaxed);
            }

            // Stopped channel becomes nullopt: the child observes the
            // requested token through the adaptor, cancels itself, the
            // cascade is absorbed at the adaptor's destroy word, and THIS
            // frame is resumed rather than destroyed
            std::optional<int> c = co_await execution::stopped_as_optional(osfs_stoppable_child());
            if (!c)
                stopped_ok->fetch_add(1, std::memory_order_relaxed);

            // shield: the outer's requested token is not the inner's, which
            // sees a detached token and completes with its value
            std::optional<int> d = co_await execution::shield(osfs_shielded_child());
            if (d && (*d == 5))
                shielded->fetch_add(1, std::memory_order_relaxed);

            // shield with a budget: the caller's own source is the inner's
            // token.  Requested here, so the inner cancels itself and the
            // adaptor absorbs that exactly as under the outer's token above
            std::stop_source budget;
            budget.request_stop();
            std::optional<int> e = co_await execution::shield(osfs_stoppable_child(), budget.get_token());
            if (!e)
                budgeted->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("execution_stopped_as_optional") {

        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> value{0};
        std::atomic<int> caught{0};
        std::atomic<int> stopped_ok{0};
        std::atomic<int> shielded{0};
        std::atomic<int> budgeted{0};

        // The scope is cancelled from the start: children that do not look
        // at the token still complete on their own channels; the one that
        // looks must complete stopped.
        source.request_stop();
        Task task = osfs_host(&value, &caught, &stopped_ok, &shielded, &budgeted);
        fence._stop_token = source.get_token();
        task._promise->set_delegate(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};

        // The host absorbed a child cancellation and still completed
        // normally through word 0
        assert(fence._outcome.load(std::memory_order_relaxed) == 1);
        assert(value.load(std::memory_order_relaxed) == 17);
        assert(caught.load(std::memory_order_relaxed) == 1);
        assert(stopped_ok.load(std::memory_order_relaxed) == 1);
        assert(shielded.load(std::memory_order_relaxed) == 1);
        assert(budgeted.load(std::memory_order_relaxed) == 1);

        co_return;
    };

    // ---- when_any, sync_wait ------------------------------------------------------

    namespace {

        Future<int> race_fast(int value) {
            co_await TransferToPoolExecutor{};
            co_return value;
        }

        // Parks on the reactor for a long time; the probe counts its unwind
        Future<int> race_slow(std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            co_await cancelable_sleep(10.0);
            co_return 99;
        }

        Future<int> race_stopper() {
            co_await SuspendAndCancel{};
            co_return 0;  // unreachable
        }

        Task race_void_fast() {
            co_await TransferToPoolExecutor{};
        }

        Task race_void_slow(std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            co_await cancelable_sleep(10.0);
        }

        Task race_host(std::atomic<int>* started,
                       std::atomic<int>* destroyed,
                       std::atomic<int>* resumed_past) {
            UnwindProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            (void) co_await execution::when_any(race_slow(destroyed), race_slow(destroyed));
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("execution_when_any") {

        // The first value wins; the interior stop hastens the parked loser,
        // which retires before the race delivers
        {
            std::atomic<int> destroyed{0};
            execution::RaceWinner<int> w = co_await execution::when_any(race_slow(&destroyed), race_fast(3));
            assert((w.index == 1) && (w.value == 3));
            assert(destroyed.load(std::memory_order_relaxed) == 1);
        }

        // Ties: both entrants complete inside start; the first claims and
        // the second's value is dropped
        {
            execution::RaceWinner<int> w = co_await execution::when_any(execution::just(1), execution::just(2));
            assert((w.index == 0) && (w.value == 1));
        }

        // An error wins like a value
        {
            std::atomic<int> destroyed{0};
            bool caught = false;
            try {
                (void) co_await execution::when_any(race_slow(&destroyed), sw_thrower());
            } catch (std::runtime_error const&) {
                caught = true;
            }
            assert(caught);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
        }

        // Every entrant stops: the race completes stopped, absorbed here
        {
            auto r = co_await stopped_as_optional(execution::when_any(race_stopper(), race_stopper()));
            assert(!r.has_value());
        }

        // void entrants
        {
            std::atomic<int> destroyed{0};
            execution::RaceWinner<void> w = co_await execution::when_any(race_void_slow(&destroyed), race_void_fast());
            assert(w.index == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
        }

        co_return;
    };

    define_test("execution_when_any_cancel") {

        using namespace std::chrono;

        // Outer cancellation bridges into the interior source: both parked
        // entrants unwind, the race completes stopped, and the host is
        // unwound through word 1 rather than resumed
        UnwindFence fence;
        std::stop_source source;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};      // two entrants + host
        std::atomic<int> resumed_past{0};

        Task task = race_host(&started, &destroyed, &resumed_past);
        fence._stop_token = source.get_token();
        task._promise->set_delegate(&fence);
        global_work_queue_schedule(handle_from_future(std::move(task)));
        while (started.load(std::memory_order_acquire) != 1)
            co_await TransferToPoolExecutor{};
        co_await cancelable_sleep(0.010);  // settle past the arming window
        auto t0 = steady_clock::now();
        source.request_stop();
        while (fence._outcome.load(std::memory_order_acquire) == 0)
            co_await TransferToPoolExecutor{};
        assert(steady_clock::now() - t0 < seconds(2));
        assert(fence._outcome.load(std::memory_order_relaxed) == 2);
        assert(destroyed.load(std::memory_order_relaxed) == 3);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);

        co_return;
    };

    define_test("execution_sync_wait") {

        // This worker blocks while another runs the sender: a value, an
        // error, a stop (the caller's token is the environment), and an
        // awaitable lifted through a coroutine
        {
            execution::Outcome<int> outcome = execution::sync_wait(sw_add(10));
            assert(outcome.value() == 14);
        }
        {
            bool caught = false;
            try {
                (void) execution::sync_wait(sw_thrower()).value();
            } catch (std::runtime_error const&) {
                caught = true;
            }
            assert(caught);
        }
        {
            std::stop_source source;
            source.request_stop();
            execution::Outcome<int> outcome = execution::sync_wait(osfs_stoppable_child(), source.get_token());
            assert(outcome.is_stopped());
        }
        {
            execution::Outcome<void> outcome = coroutine::sync_wait(TransferToPoolExecutor{});
            assert(outcome.has_value());
        }

        // A nursery's join, as WorldState::update does it: the join is a
        // single-use awaitable whose copies assert when they die unawaited,
        // so the lift must await the caller's object in place
        {
            std::atomic<int> ran{0};
            execution::Outcome<int> target;
            Nursery nursery;
            nursery.soon(sw_void_child(&ran));
            nursery.soon(target, sw_add(0));
            execution::Outcome<std::ptrdiff_t> outcome = sync_wait(nursery.join());
            assert(outcome.value() == 0);
            assert(ran.load(std::memory_order_relaxed) == 1);
            assert(target.value() == 4);
        }

        co_return;
    };

} // namespace wry::coroutine
