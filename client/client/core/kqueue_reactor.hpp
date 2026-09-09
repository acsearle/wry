//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <chrono>
#include <expected>
#include <span>

#include "coroutine.hpp"

namespace wry {

    // A dedicated reactor thread blocked in kevent waits on kernel events
    // (fd readiness, timers) and reschedules the interested coroutines onto
    // the global work queue.  The reactor is a foreign thread: it never runs
    // GC code and never resumes or destroys frames itself -- completions and
    // cancellations are handed to the (mutator-pinned) worker pool.
    //
    //   macOS    : kqueue
    //   Windows  : IOCP
    //   Linux    : epoll (io_uring?)
    //   Fallback : select

    // Called once by main() before any IO
    void global_reactor_start();

    // Trigger the shutdown doorbell and join the reactor thread.  No-op if
    // the reactor was never started.  Call after all waits have completed
    // (the wait group has drained) and before the worker pool is cancelled.
    void global_reactor_stop();

    // At `duration` from now, start `future` on the pool with `token` as its
    // stop token -- unless `token` is requested first, in which case the
    // future never runs.  After request_stop(token) returns, the timer
    // provably will not fire and the future provably will not run:
    // cancellation is a synchronous join.  A failed timer registration
    // aborts (it is resource-exhaustion grade).
    void cancelable_after(std::stop_token token,
                          std::chrono::steady_clock::duration duration,
                          Coroutine::Future<>&& future);

    // Suspend until `socket` is readable, then perform one recv.  Returns the
    // recv result in kernel convention: bytes received, 0 for end-of-stream
    // (peer close arrives as EV_EOF readability), or -errno for a failed
    // registration (e.g. -EBADF) or a failed recv.
    //
    // No deadline here: compose one with with_deadline (kqueue_reactor.cpp),
    // which times out the wait by requesting an interior stop source.  A stop
    // request on the awaiting chain's token destroys the parked frame
    // (cancellation unwinding); the knote is withdrawn promptly.  At most one
    // recv_some may wait on a given socket at a time: a second waiter's knote
    // also fires on readability, and on a blocking socket the loser blocks
    // its worker in recv.
    [[nodiscard]] Coroutine::Future<ssize_t>
    recv_some(int socket, void* buffer, size_t length, int flags);

    [[nodiscard]] Coroutine::Future<>
    cancelable_sleep(double seconds);





    template<typename OuterPromise, typename T>
    struct BasicWithDeadlineAwaitable : Coroutine::Delegate<T> {

        struct Frame {

            struct Callback {
                std::stop_source _stop_source;
                void operator()() {
                    // Local copy to survive possible self-destruct
                    std::stop_source stop_source{std::move(_stop_source)};
                    stop_source.request_stop();
                }
            };

            Coroutine::Header _header = { &_static_resume, &_static_destroy };

            OuterPromise* _outer_promise;
            std::chrono::steady_clock::time_point _deadline;
            Coroutine::Future<T> _future;
            std::stop_source _inner_stop_source;
            std::optional<std::stop_callback<Callback>> _stop_callback;
            Coroutine::Outcome<T> _outcome;

            Frame(OuterPromise* outer_promise,
                  std::chrono::steady_clock::time_point deadline,
                  Coroutine::Future<T> future)
            : _outer_promise(std::move(outer_promise))
            , _deadline(std::move(deadline))
            , _future(std::move(future)) {
            }

#define CONTINUE \
if (Coroutine::get_stop_token(*(self->_outer_promise)).stop_requested()) { \
[[clang::musttail]] return Coroutine::destroy_by_address(Coroutine::handle_from_promise(self->_outer_promise).address()); \
} else { \
[[clang::musttail]] return Coroutine::resume_by_address(Coroutine::handle_from_promise(self->_outer_promise).address()); \
}

            static void _static_resume(void* ptr) {
                auto self = (Frame*)ptr;
                CONTINUE
            }

            static void _static_destroy(void* ptr) {
                auto self = (Frame*)ptr;
                // The inner promise recorded STOPPED in _outcome before destroying
                // us (its continuation); this word only continues
                assert(self->_outcome.is_stopped());
                CONTINUE
            }

#undef CONTINUE

        } _frame;


        BasicWithDeadlineAwaitable(OuterPromise* outer_promise,
                                   std::chrono::steady_clock::time_point deadline,
                                   Coroutine::Future<T> future)
        : _frame{outer_promise, std::move(deadline), std::move(future)} {
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise>) noexcept {
            using Coroutine::get_stop_token;
            _frame._future._promise->set_delegate(this);
            _frame._stop_callback.emplace(get_stop_token(*_frame._outer_promise),
                                          typename Frame::Callback{_frame._inner_stop_source});
            cancelable_after(_frame._inner_stop_source.get_token(),
                             _frame._deadline - std::chrono::steady_clock::now(),
                             [](std::stop_source stop_source) -> Coroutine::Future<> {
                (void) stop_source.request_stop();
                co_return;
            } (_frame._inner_stop_source));
            return handle_from_future(std::move(_frame._future));
        }

        std::optional<T> await_resume() {
            _frame._inner_stop_source.request_stop();
            return _frame._outcome.stopped_as_optional();
        }

        virtual void unhandled_exception() noexcept override {
            set_error(_frame._outcome, std::current_exception());
        };

        virtual void unhandled_stopped() noexcept override {
            handle_from_promise(std::exchange(_frame._outer_promise, nullptr)).destroy();
        }

        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return handle_from_promise(std::exchange(_frame._outer_promise, nullptr));
        }

        virtual std::stop_token get_stop_token() noexcept override {
            using Coroutine::get_stop_token;
            return get_stop_token(*_frame._outer_promise);
        }

    };

    template<typename OuterPromise, typename T>
    struct WithDeadlineAwaitable : BasicWithDeadlineAwaitable<OuterPromise, T> {
        using BasicWithDeadlineAwaitable<OuterPromise, T>::BasicWithDeadlineAwaitable;
        virtual void return_value(T value) noexcept override {
            set_value(this->_frame._outcome, std::move(value));
        }
    };

    template<typename OuterPromise>
    struct WithDeadlineAwaitable<OuterPromise, void> : BasicWithDeadlineAwaitable<OuterPromise, void> {
        using BasicWithDeadlineAwaitable<OuterPromise, void>::BasicBasicWithDeadlineAwaitable;
        virtual void return_void() noexcept override {
            set_value(this->_frame._outcome);
        }
    };

    template<typename T>
    struct WithDeadlineSender {
        std::chrono::steady_clock::time_point _deadline;
        Coroutine::Future<T> _future;
    };

//    template<typename T>
//    Coroutine::Future<std::optional<T>> with_deadline(std::chrono::steady_clock::time_point deadline, Coroutine::Future<T>&& future) {
//        co_return co_await WithDeadlineAwaitable<T>(std::move(deadline), std::move(future));
//    };

    template<typename T>
    WithDeadlineSender<T> with_deadline(std::chrono::steady_clock::time_point deadline, Coroutine::Future<T>&& future) {
        return WithDeadlineSender<T>(std::move(deadline), std::move(future));
    };

    template<typename OuterPromise, typename T>
    auto await_transform_helper(OuterPromise* outer_promise, WithDeadlineSender<T>&& s) {
        return WithDeadlineAwaitable<OuterPromise, T>{outer_promise, std::move(s._deadline), std::move(s._future)};
    }





} // namespace wry

#endif /* kqueue_reactor_hpp */
