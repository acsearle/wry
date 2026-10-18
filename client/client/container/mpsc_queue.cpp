//
//  mpsc_queue.cpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#include <atomic>
#include <cassert>
#include <deque>
#include <memory>

#include "mpsc_queue.hpp"
#include "coroutine_test.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "test.hpp"

namespace wry::coroutine {

    // ---- MultiProducerSingleConsumerQueue --------------------------------------

    namespace {

        using IntQueue = MultiProducerSingleConsumerQueue<int>;
        static_assert(std::is_same_v<SingleProducerSingleConsumerQueue<int>, IntQueue>);

        // Takes batches forever, summing and counting batches; only
        // cancellation ends it
        Task spsc_taker(IntQueue* queue,
                        std::atomic<int>* batches,
                        std::atomic<int>* sum,
                        std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            for (;;) {
                std::deque<int> batch = co_await queue->take();
                assert(!batch.empty());
                for (int value : batch)
                    sum->fetch_add(value, std::memory_order_relaxed);
                batches->fetch_add(1, std::memory_order_release);
            }
        }

        // Producers on their own frames: each emplaces its range
        Task spsc_producer(IntQueue* queue, int first, int last) {
            for (int i = first; i != last; ++i) {
                queue->emplace(i);
                if (i % 5 == 0)
                    co_await TransferToPoolExecutor{};
            }
        }

        // Pops forever, summing; only cancellation ends it.  The probe
        // counts the frame's destruction; consumed counts deliveries
        Task spsc_consumer(IntQueue* queue,
                           std::atomic<int>* consumed,
                           std::atomic<int>* sum,
                           std::atomic<int>* destroyed) {
            UnwindProbe probe{destroyed};
            for (;;) {
                int value = co_await queue->pop();
                sum->fetch_add(value, std::memory_order_relaxed);
                consumed->fetch_add(1, std::memory_order_release);
            }
        }

        // Pops exactly n items, then completes normally
        Task spsc_consumer_n(IntQueue* queue, int n, std::atomic<int>* sum) {
            for (int i = 0; i != n; ++i) {
                int value = co_await queue->pop();
                sum->fetch_add(value, std::memory_order_relaxed);
            }
        }

    }

    define_test("mpsc_queue") {

        // Ready path: items already queued are popped in order without
        // parking, and try_pop sees what pop sees.  Move-only elements pin
        // the move through emplace and pop
        {
            SingleProducerSingleConsumerQueue<std::unique_ptr<int>> queue;
            std::unique_ptr<int> victim;
            [[maybe_unused]] bool popped = queue.try_pop(victim);
            assert(!popped);
            queue.emplace(std::make_unique<int>(1));
            queue.emplace(std::make_unique<int>(2));
            queue.emplace(std::make_unique<int>(3));
            std::unique_ptr<int> a = co_await queue.pop();
            assert(a && (*a == 1));
            popped = queue.try_pop(victim);
            assert(popped && victim && (*victim == 2));
            std::unique_ptr<int> c = co_await queue.pop();
            assert(c && (*c == 3));
            popped = queue.try_pop(victim);
            assert(!popped);
        }

        // Parked path: the consumer parks on an empty queue and a later
        // emplace from this frame schedules it; repeated, with the producer
        // yielding between items so some pops park and some find an item
        {
            IntQueue queue;
            std::atomic<int> sum{0};
            Nursery nursery;
            const int n = 200;
            nursery.soon(spsc_consumer_n(&queue, n, &sum));
            for (int i = 1; i <= n; ++i) {
                queue.emplace(i);
                if (i % 3 == 0)
                    co_await TransferToPoolExecutor{};
                if (i % 7 == 0)
                    co_await cancelable_sleep(0.0005);
            }
            [[maybe_unused]] auto canceled_count = co_await nursery.join();
            assert(canceled_count == 0);
            assert(sum.load(std::memory_order_relaxed) == n * (n + 1) / 2);
            int leftover = 0;
            [[maybe_unused]] bool popped = queue.try_pop(leftover);
            assert(!popped);
        }

        // take: everything queued in one go, in order; try_take likewise,
        // and empty when nothing is queued
        {
            IntQueue queue;
            [[maybe_unused]] std::deque<int> none = queue.try_take();
            assert(none.empty());
            queue.emplace(1);
            queue.emplace(2);
            queue.emplace(3);
            std::deque<int> all = co_await queue.take();
            assert((all.size() == 3) && (all[0] == 1) && (all[1] == 2) && (all[2] == 3));
            none = queue.try_take();
            assert(none.empty());
            queue.emplace(4);
            std::deque<int> one = queue.try_take();
            assert((one.size() == 1) && (one[0] == 4));
        }

        // Multiple producers, one taker: four producer frames interleave on
        // the pool while the taker drains batches; every item arrives once.
        // A take parks only on an empty queue, so the batch count is at
        // most the item count and at least one
        {
            IntQueue queue;
            std::atomic<int> batches{0}, sum{0}, destroyed{0};
            Nursery takers;
            takers.soon(spsc_taker(&queue, &batches, &sum, &destroyed));
            const int n = 400;
            {
                Nursery producers;
                for (int p = 0; p != 4; ++p)
                    producers.soon(spsc_producer(&queue, p * 100 + 1, p * 100 + 101));
                [[maybe_unused]] std::ptrdiff_t cancelled = co_await producers.join();
                assert(cancelled == 0);
            }
            while (sum.load(std::memory_order_relaxed) != n * (n + 1) / 2)
                co_await TransferToPoolExecutor{};
            int b = batches.load(std::memory_order_acquire);
            assert((b >= 1) && (b <= n));
            takers.request_stop();
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await takers.join();
            assert(cancelled == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            [[maybe_unused]] std::deque<int> none = queue.try_take();
            assert(none.empty());
        }

        co_return;
    };

    define_test("mpsc_queue_cancel") {

        // Parked cancel: the consumer is parked on an empty queue; the stop
        // request destroys its frame synchronously, inside request_stop,
        // through the callback that takes the parked continuation.  The
        // queue is intact afterwards: no continuation left behind, items
        // still flow
        {
            IntQueue queue;
            std::atomic<int> consumed{0}, sum{0}, destroyed{0};
            Nursery nursery;
            nursery.soon(spsc_consumer(&queue, &consumed, &sum, &destroyed));
            co_await cancelable_sleep(0.010);  // let it park
            nursery.request_stop();
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(consumed.load(std::memory_order_relaxed) == 0);
            assert(!queue._pop_continuation);
            queue.emplace(5);
            int value = 0;
            [[maybe_unused]] bool popped = queue.try_pop(value);
            assert(popped && (value == 5));
        }

        // Parked cancel of a taker: the same callback path
        {
            IntQueue queue;
            std::atomic<int> batches{0}, sum{0}, destroyed{0};
            Nursery nursery;
            nursery.soon(spsc_taker(&queue, &batches, &sum, &destroyed));
            co_await cancelable_sleep(0.010);  // let it park
            nursery.request_stop();
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(batches.load(std::memory_order_relaxed) == 0);
            assert(!queue._pop_continuation);
        }

        // Cancel before park: the token is already requested when the
        // consumer reaches its first pop, so the pop destroys the frame
        // inline, from await_suspend, without ever parking
        {
            IntQueue queue;
            std::atomic<int> consumed{0}, sum{0}, destroyed{0};
            Nursery nursery;
            nursery.request_stop();
            nursery.soon(spsc_consumer(&queue, &consumed, &sum, &destroyed));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(consumed.load(std::memory_order_relaxed) == 0);
            assert(!queue._pop_continuation);
        }

        // An item delivered before the stop stays delivered, and the
        // consumer's NEXT pop is the cancellation point: whether it has
        // parked there already (the callback destroys it) or reaches it
        // after the request (await_suspend destroys it inline)
        {
            IntQueue queue;
            std::atomic<int> consumed{0}, sum{0}, destroyed{0};
            Nursery nursery;
            nursery.soon(spsc_consumer(&queue, &consumed, &sum, &destroyed));
            co_await cancelable_sleep(0.010);  // let it park
            queue.emplace(7);
            while (consumed.load(std::memory_order_acquire) != 1)
                co_await TransferToPoolExecutor{};
            nursery.request_stop();
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(sum.load(std::memory_order_relaxed) == 7);
        }

        // A non-empty queue under a requested token: pop prefers
        // cancellation, and the item stays queued for a later consumer
        {
            IntQueue queue;
            std::atomic<int> consumed{0}, sum{0}, destroyed{0};
            Nursery nursery;
            nursery.request_stop();
            queue.emplace(8);
            nursery.soon(spsc_consumer(&queue, &consumed, &sum, &destroyed));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            assert(cancelled == 1);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(consumed.load(std::memory_order_relaxed) == 0);
            int value = 0;
            [[maybe_unused]] bool popped = queue.try_pop(value);
            assert(popped && (value == 8));
        }

        co_return;
    };

} // namespace wry::coroutine
