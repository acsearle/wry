//
//  single_consumer_event.cpp
//  client
//
//  Created by Antony Searle on 18/10/2026.
//

#include <atomic>
#include <cassert>
#include <chrono>
#include <optional>

#include "single_consumer_event.hpp"
#include "coroutine_test.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "test.hpp"

namespace wry::coroutine {

    namespace {

        // Waits on the event, then counts; the probe counts the frame's
        // destruction whichever way it ends
        Task event_waiter(SingleConsumerEvent* event,
                          std::atomic<int>* started,
                          std::atomic<int>* resumed,
                          std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            co_await *event;
            resumed->fetch_add(1, std::memory_order_release);
        }

        // Sets the event after a delay, from its own frame
        Task late_setter(SingleConsumerEvent* event) {
            co_await cancelable_sleep(0.010);
            event->set();
        }

    }

    define_test("single_consumer_event") {

        // Set before the await: completes at once, and set is idempotent
        {
            SingleConsumerEvent event;
            assert(!event.is_set());
            event.set();
            event.set();
            assert(event.is_set());
            co_await event;
            assert(event.is_set());
        }

        // Parked: the waiter parks, a later set from another frame wakes it
        {
            SingleConsumerEvent event;
            std::atomic<int> started{0}, resumed{0}, destroyed{0};
            Nursery nursery;
            nursery.soon(event_waiter(&event, &started, &resumed, &destroyed));
            while (started.load(std::memory_order_acquire) != 1)
                co_await TransferToPoolExecutor{};
            nursery.soon(late_setter(&event));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 0);
            assert(resumed.load(std::memory_order_relaxed) == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(event.is_set());
        }

        // Under a deadline: the deadline path is nullopt, the set path is
        // the value
        {
            using namespace std::chrono;
            SingleConsumerEvent event;
            auto nothing = co_await with_deadline(steady_clock::now() + milliseconds(20), event);
            assert(!nothing.has_value());
            Nursery nursery;
            nursery.soon(late_setter(&event));
            auto something = co_await with_deadline(steady_clock::now() + seconds(5), event);
            assert(something.has_value());
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 0);
        }

        co_return;
    };

    define_test("single_consumer_event_cancel") {

        // Parked cancel: the stop request withdraws the frame and destroys
        // it inside request_stop; the event is back to unset, and a later
        // set finds nobody waiting
        {
            SingleConsumerEvent event;
            std::atomic<int> started{0}, resumed{0}, destroyed{0};
            Nursery nursery;
            nursery.soon(event_waiter(&event, &started, &resumed, &destroyed));
            while (started.load(std::memory_order_acquire) != 1)
                co_await TransferToPoolExecutor{};
            co_await cancelable_sleep(0.010);  // let it park
            nursery.request_stop();
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(resumed.load(std::memory_order_relaxed) == 0);
            assert(event._state.load(std::memory_order_relaxed) == nullptr);
            event.set();
            assert(event.is_set());
        }

        // Cancel before park: the token is requested when the waiter
        // arrives, so the callback marks the event and the publish refuses;
        // the frame is destroyed inline
        {
            SingleConsumerEvent event;
            std::atomic<int> started{0}, resumed{0}, destroyed{0};
            Nursery nursery;
            nursery.request_stop();
            nursery.soon(event_waiter(&event, &started, &resumed, &destroyed));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(started.load(std::memory_order_relaxed) == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(resumed.load(std::memory_order_relaxed) == 0);
            // The mark is stale now; a fresh waiter under a live token
            // clears it and sees the event unset until set
            Nursery again;
            std::atomic<int> started2{0}, resumed2{0}, destroyed2{0};
            again.soon(event_waiter(&event, &started2, &resumed2, &destroyed2));
            while (started2.load(std::memory_order_acquire) != 1)
                co_await TransferToPoolExecutor{};
            event.set();
            cancelled = co_await again.join();
            assert(cancelled == 0);
            assert(resumed2.load(std::memory_order_relaxed) == 1);
        }

        // Set and requested: cancellation wins, as for the queue
        {
            SingleConsumerEvent event;
            event.set();
            std::atomic<int> started{0}, resumed{0}, destroyed{0};
            Nursery nursery;
            nursery.request_stop();
            nursery.soon(event_waiter(&event, &started, &resumed, &destroyed));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(resumed.load(std::memory_order_relaxed) == 0);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
        }

        co_return;
    };

} // namespace wry::coroutine
