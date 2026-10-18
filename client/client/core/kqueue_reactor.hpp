//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <sys/socket.h>

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

    // Socket operations.  Each suspends until the descriptor is ready for
    // it (a readiness awaitable on EVFILT_READ or EVFILT_WRITE, see the
    // .cpp) and then makes one syscall, whose result is returned in kernel
    // convention: the syscall's value on success, or -errno -- for the
    // syscall, or for a failed knote registration (-EBADF for a closed
    // descriptor).  Readiness is not a guarantee: the syscall may still
    // fail with -EAGAIN on a non-blocking descriptor if another waiter took
    // the data, and will block the worker on a blocking one, so at most one
    // waiter per (descriptor, direction) at a time.  Peer close arrives as
    // readiness, and the syscall reports it (recv 0, send -EPIPE; set
    // SO_NOSIGPIPE on the socket, or SIGPIPE comes first).
    //
    // No deadlines here: compose one with with_deadline below, which times
    // the wait out by requesting an interior stop source.  A stop request on
    // the awaiting chain's token destroys the parked frame (cancellation
    // unwinding) and withdraws the knote promptly.

    // recv: bytes received, 0 for end of stream, or -errno
    [[nodiscard]] coroutine::Future<ssize_t>
    recv_some(int socket, void* buffer, size_t length, int flags);

    // send: bytes sent (possibly fewer than asked), or -errno
    [[nodiscard]] coroutine::Future<ssize_t>
    send_some(int socket, void const* buffer, size_t length, int flags);

    // accept on a listening socket: the new descriptor, or -errno.  The new
    // descriptor inherits nothing from the reactor: set O_NONBLOCK and
    // SO_NOSIGPIPE on it as the caller sees fit.  (Named accept_one, and
    // connect_to below, because the C names would be ambiguous through
    // argument-dependent lookup on sockaddr)
    [[nodiscard]] coroutine::Future<int>
    accept_one(int socket, struct sockaddr* address, socklen_t* length);

    // connect: 0, or -errno.  Suspends only when the socket is non-blocking
    // and the connect is in progress (TCP); a blocking socket connects
    // synchronously on the worker
    [[nodiscard]] coroutine::Future<int>
    connect_to(int socket, struct sockaddr const* address, socklen_t length);

    [[nodiscard]] coroutine::Future<>
    cancelable_sleep(double seconds);


    template<typename OuterPromise, typename T>
    struct WithDeadlineAwaitable : coroutine::OutcomeDelegate<WithDeadlineAwaitable<OuterPromise, T>, T> {

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
            coroutine::Future<T> _future;
            std::stop_source _inner_stop_source;
            std::optional<std::stop_callback<Callback>> _stop_callback;
            execution::Outcome<T> _outcome;

            Frame(OuterPromise* outer_promise,
                  std::chrono::steady_clock::time_point deadline,
                  coroutine::Future<T> future)
            : _outer_promise(std::move(outer_promise))
            , _deadline(std::move(deadline))
            , _future(std::move(future)) {
            }


        } _frame;


        WithDeadlineAwaitable(OuterPromise* outer_promise,
                              std::chrono::steady_clock::time_point deadline,
                              coroutine::Future<T> future)
        : _frame{outer_promise, std::move(deadline), std::move(future)} {
        }

        execution::Outcome<T>& outcome() noexcept {
            return _frame._outcome;
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise>) noexcept {
            using coroutine::get_stop_token;
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

        // optional<monostate> for a void inner, as stopped_as_optional yields
        std::optional<typename execution::Outcome<T>::stored_type> await_resume() {
            _frame._inner_stop_source.request_stop();
            return _frame._outcome.stopped_as_optional();
        }

        virtual void unhandled_stopped() noexcept override {
            // Absorb: the inner stopped (usually by our own timer), so the
            // outer resumes with nullopt -- unless the outer's own token is
            // requested, in which case the unwinding continues through it
            using coroutine::get_stop_token;
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

    template<typename T>
    struct WithDeadlineSender {
        std::chrono::steady_clock::time_point _deadline;
        coroutine::Future<T> _future;
    };

//    template<typename T>
//    coroutine::Future<std::optional<T>> with_deadline(std::chrono::steady_clock::time_point deadline, coroutine::Future<T>&& future) {
//        co_return co_await WithDeadlineAwaitable<T>(std::move(deadline), std::move(future));
//    };

    template<typename T>
    WithDeadlineSender<T> with_deadline(std::chrono::steady_clock::time_point deadline, coroutine::Future<T>&& future) {
        return WithDeadlineSender<T>(std::move(deadline), std::move(future));
    };

    // Anything else a coroutine could co_await (a queue's take, a nursery's
    // join) is lifted into a Future first, awaited in place through a
    // pointer: the argument must outlive the await, which a temporary in
    // the awaiting expression does.  Senders other than Future are not yet
    // accepted (WithDeadlineSender has no connect)
    template<typename A>
        requires (!execution::Sender<A>)
    auto with_deadline(std::chrono::steady_clock::time_point deadline, A&& awaitable) {
        return with_deadline(deadline, coroutine::lift_awaitable<A>(&awaitable));
    }

    template<typename U, typename T>
    auto transform_await(coroutine::Promise<U>* outer_promise, WithDeadlineSender<T>&& s) {
        return WithDeadlineAwaitable<coroutine::Promise<U>, T>(outer_promise, std::move(s._deadline), std::move(s._future));
    }


} // namespace wry

#endif /* kqueue_reactor_hpp */
