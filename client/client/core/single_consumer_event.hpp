//
//  single_consumer_event.hpp
//  client
//
//  Created by Antony Searle on 18/10/2026.
//

#ifndef single_consumer_event_hpp
#define single_consumer_event_hpp

#include <atomic>
#include <coroutine>
#include <optional>
#include <stop_token>

#include "coroutine.hpp"

namespace wry::coroutine {

    // A latching, single-shot event with one awaiter at a time.  set() from
    // any thread, idempotently; co_await parks the frame until it is set,
    // or completes at once if it already is.  A stop request on the
    // awaiter's token destroys the parked frame (cancellation unwinding); an
    // awaiter arriving with its token already requested is destroyed
    // without parking, set or not.
    //
    // One atomic word carries the state:
    //
    //   nullptr        not set, nobody waiting
    //   a frame handle not set, that frame is parked
    //   this           set
    //   &g_cancelled   not set; the waiter's stop callback fired before the
    //                  frame was published, and the publish must not proceed
    //
    // set() exchanges in the sentinel and schedules a parked frame; that
    // exchange is its LAST touch, so the woken frame may destroy the event.
    // The waiter publishes its handle with a compare-exchange from nullptr;
    // the stop callback withdraws it with a compare-exchange back, and only
    // the winner of that race touches the frame.  A setter must reach the
    // event through something that outlives the set (the control socket
    // publishes a pointer under its mutex and clears it on every exit).
    //
    // Each co_await makes an Awaiter in the awaiting frame, holding that
    // wait's stop callback: a cancelled wait's callback runs with the frame
    // it is destroying, and the storage must go with the frame, not be
    // reused by the next wait on the same event.

    struct SingleConsumerEvent {

        std::atomic<void*> _state{nullptr};

        inline static std::byte g_cancelled;
        static void* cancelled_marker() noexcept { return &g_cancelled; }
        void* set_marker() noexcept { return this; }

        struct Callback {
            SingleConsumerEvent* _event;
            std::coroutine_handle<> _handle;
            void operator()() {
                // Withdraw the parked frame, if it was published; else mark
                // the event so the publish refuses.  Acquire on success: we
                // are about to destroy the frame, or the publisher reads the
                // mark
                void* expected = _handle.address();
                if (_event->_state.compare_exchange_strong(expected, nullptr,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_relaxed)) {
                    _handle.destroy();   // *this, in the frame, dies here
                    return;
                }
                expected = nullptr;
                (void) _event->_state.compare_exchange_strong(expected, cancelled_marker(),
                                                              std::memory_order_acq_rel,
                                                              std::memory_order_relaxed);
                // set already, or the frame was withdrawn: nothing to do
            }
        };

        SingleConsumerEvent() = default;
        SingleConsumerEvent(SingleConsumerEvent const&) = delete;

        bool is_set() const noexcept {
            return _state.load(std::memory_order_acquire) == this;
        }

        // Any thread, idempotent.  The exchange is the last touch of the
        // event; only a parked frame is scheduled, not either marker
        void set() noexcept {
            void* old = _state.exchange(set_marker(), std::memory_order_acq_rel);
            if (old && (old != set_marker()) && (old != cancelled_marker()))
                global_work_queue_schedule(std::coroutine_handle<>::from_address(old));
        }

        struct Awaiter {

            SingleConsumerEvent* _event;
            std::optional<std::stop_callback<Callback>> _stop_callback;

            // Never ready: a set event still goes through await_suspend,
            // where a requested token wins over the set (as for the queue),
            // and a plain set resumes inline from there
            constexpr bool await_ready() const noexcept {
                return false;
            }

            template<typename OuterPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
                std::stop_token stop_token = get_stop_token(continuation);
                // A previous waiter's cancel mark is stale: this waiter
                // starts clean.  Single consumer, so nothing races this
                // but set()
                void* expected = cancelled_marker();
                (void) _event->_state.compare_exchange_strong(expected, nullptr,
                                                              std::memory_order_relaxed,
                                                              std::memory_order_relaxed);
                // Register first, publish second: a request landing between
                // the two leaves the mark, and the publish below refuses it
                _stop_callback.emplace(stop_token, Callback{_event, continuation});
                expected = nullptr;
                if (_event->_state.compare_exchange_strong(expected, continuation.address(),
                                                           std::memory_order_release,
                                                           std::memory_order_acquire)) {
                    // Parked: no *this past here, the callback or set()
                    // owns the frame now
                    return std::noop_coroutine();
                }
                // Not published: set already, or marked.  Either way the
                // frame is still ours.  A requested token wins over a set
                if ((expected == cancelled_marker()) || stop_token.stop_requested()) {
                    continuation.destroy();
                    return std::noop_coroutine();
                }
                return continuation;   // set: resume inline
            }

            void await_resume() const noexcept {}

        };

        Awaiter operator co_await() noexcept {
            return Awaiter{this};
        }

    };

} // namespace wry::coroutine

#endif /* single_consumer_event_hpp */
