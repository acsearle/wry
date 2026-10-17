//
//  mpsc_queue.hpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#ifndef mpsc_queue_hpp
#define mpsc_queue_hpp

#include <deque>
#include <mutex>

#include "coroutine.hpp"

namespace wry::coroutine {
    // ---- MultiProducerSingleConsumerQueue ----------------------------------
    //
    // Any thread may emplace; one consumer at a time pops (one item), takes
    // (everything queued) or tries either without waiting.  The waiting
    // forms park the consumer's frame on an empty queue, to be scheduled
    // by the next emplace; a stop request on its token destroys the parked
    // frame (cancellation unwinding), and a consumer that arrives with its
    // token already requested is destroyed without parking, even if items
    // are waiting.  An item is delivered once its emplace has scheduled the
    // consumer, requested token or not.

    template<typename T>
    struct MultiProducerSingleConsumerQueue {

        std::mutex _mutex;
        std::deque<T> _queue;
        std::coroutine_handle<> _pop_continuation;

        void emplace(auto&&... args) {
            std::unique_lock guard{_mutex};
            _queue.emplace_back(FORWARD(args)...);
            auto continuation = wry::take(_pop_continuation);
            if (continuation)
                global_work_queue_schedule(continuation);
        }

        // The parking common to pop and take; the derived await_resume says
        // how much of the queue the consumer gets
        struct BasicAwaitable {

            struct Callback {
                MultiProducerSingleConsumerQueue* _context;
                void operator()() {
                    std::coroutine_handle<> continuation;
                    {
                        std::unique_lock guard{_context->_mutex};
                        continuation = wry::take(_context->_pop_continuation);
                    }
                    if (continuation)
                        continuation.destroy();
                }
            };

            MultiProducerSingleConsumerQueue* _context;
            std::optional<std::stop_callback<Callback>> _stop_callback;

            explicit BasicAwaitable(MultiProducerSingleConsumerQueue* context)
            : _context(context) {
            }

            constexpr bool await_ready() const noexcept {
                return false;
            }

            template<typename OuterPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {

                auto stop_token = get_stop_token(continuation);
                _stop_callback.emplace(stop_token, Callback{_context});
                {
                    std::unique_lock guard{_context->_mutex};
                    assert(!_context->_pop_continuation); // single consumer violated
                    if (!stop_token.stop_requested()) {
                        if (_context->_queue.empty()) {
                            _context->_pop_continuation = continuation;
                            return std::noop_coroutine();
                        }
                        return continuation;
                    }
                }
                continuation.destroy();
                return std::noop_coroutine();
            }

        };

        struct PopAwaitable : BasicAwaitable {
            using BasicAwaitable::BasicAwaitable;
            T await_resume() {
                std::unique_lock guard{this->_context->_mutex};
                assert(!this->_context->_queue.empty());
                T result{std::move(this->_context->_queue.front())};
                this->_context->_queue.pop_front();
                return result;
            }
        };

        struct TakeAwaitable : BasicAwaitable {
            using BasicAwaitable::BasicAwaitable;
            std::deque<T> await_resume() {
                std::unique_lock guard{this->_context->_mutex};
                assert(!this->_context->_queue.empty());
                return std::exchange(this->_context->_queue, {});
            }
        };

        // One item, waiting for it
        [[nodiscard]] PopAwaitable pop() {
            return PopAwaitable{this};
        };

        // Everything queued, in order, waiting for at least one item
        [[nodiscard]] TakeAwaitable take() {
            return TakeAwaitable{this};
        };

        bool try_pop(T& victim) {
            std::unique_lock guard{_mutex};
            assert(!_pop_continuation); // single consumer violated
            bool result = !_queue.empty();
            if (result) {
                victim = std::move(_queue.front());
                _queue.pop_front();
            }
            return result;
        }

        // Everything queued, in order; empty if nothing is
        [[nodiscard]] std::deque<T> try_take() {
            std::unique_lock guard{_mutex};
            assert(!_pop_continuation); // single consumer violated
            return std::exchange(_queue, {});
        }

        // TODO: Sender/receiver-level interface?

    };

    template<typename T>
    using SingleProducerSingleConsumerQueue = MultiProducerSingleConsumerQueue<T>;

} // namespace wry::coroutine


#endif /* mpsc_queue_hpp */
