//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <chrono>
#include <functional>
#include <expected>
#include <span>

#include "coroutine.hpp"
#include "functional.hpp"

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


    // At `duration` from now, invoke `f` on the pool -- unless `token` is
    // requested first, in which case `f` is destroyed unrun.  A stop request
    // against an unfired timer withdraws it synchronously; a timer that has
    // already fired runs `f` regardless, so `f` must tolerate a late stop
    // (with_deadline's does: a second request_stop is idempotent).  A failed
    // timer registration aborts (it is resource-exhaustion grade).
    void cancelable_after(std::stop_token token,
                          std::chrono::steady_clock::duration duration,
                          move_only_function<void()> f);

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
                             [stop_source = _frame._inner_stop_source]() mutable {
                // Local copy: the callbacks this fires may unwind the owner
                std::stop_source keepalive = stop_source;
                keepalive.request_stop();
            });
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
            // Absorb: the inner stopped (usually by our own timer), so the
            // outer resumes with nullopt -- unless the outer's own token is
            // requested, in which case the unwinding continues through it
            using Coroutine::get_stop_token;
            set_stopped(_frame._outcome);
            OuterPromise* outer = take(_frame._outer_promise);
            if (get_stop_token(*outer).stop_requested())
                handle_from_promise(outer).destroy();
            else
                handle_from_promise(outer).resume();
        }

        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return handle_from_promise(take(_frame._outer_promise));
        }

        virtual std::stop_token get_stop_token() noexcept override {
            // The inner runs under the interior source, which the timer
            // requests at the deadline and the outer's token is bridged to
            return _frame._inner_stop_source.get_token();
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
        using BasicWithDeadlineAwaitable<OuterPromise, void>::BasicWithDeadlineAwaitable;
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

    template<typename U, typename T>
    auto await_transform_helper(Coroutine::Promise<U>* outer_promise, WithDeadlineSender<T>&& s) {
        return WithDeadlineAwaitable<Coroutine::Promise<U>, T>(outer_promise, std::move(s._deadline), std::move(s._future));
    }


} // namespace wry

#endif /* kqueue_reactor_hpp */
