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





    template<typename T>
    struct WithDeadlineAwaitable {

        struct Callback {
            std::stop_source _stop_source;
            void operator()() {
                // Local copy to survive possible self-destruct
                std::stop_source stop_source{std::move(_stop_source)};
                stop_source.request_stop();
            }
        };

        Coroutine::Header _header = { &_static_resume, &_static_destroy };

        std::chrono::steady_clock::time_point _deadline;
        Coroutine::Future<T> _future;
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;
        std::optional<std::stop_callback<Callback>> _stop_callback;

        Coroutine::Outcome<T> _outcome;

        WithDeadlineAwaitable(std::chrono::steady_clock::time_point deadline,
                              Coroutine::Future<T> future)
        : _deadline(std::move(deadline))
        , _future(std::move(future)) {
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            _continuation = handle;
            using Coroutine::get_stop_token;
            _outer_stop_token = get_stop_token(handle);
            _stop_callback.emplace(_outer_stop_token, Callback{_inner_stop_source});
            cancelable_after(_inner_stop_source.get_token(),
                             _deadline - std::chrono::steady_clock::now(),
                             [](std::stop_source stop_source) mutable -> Coroutine::Future<> {
                // request_stop returns bool; a non-void co_return would route
                // to return_value, which Promise<void> lacks
                (void) stop_source.request_stop();
                co_return;
            } (_inner_stop_source));
            _future._promise->set_stop_token(_inner_stop_source.get_token());
            _future._promise->set_target(&_outcome);
            _future._promise->set_continuation(std::coroutine_handle<>::from_address(this));
            return handle_from_future(std::move(_future));
        }

#define CONTINUE \
if (self->_outer_stop_token.stop_requested()) { \
[[clang::musttail]] return Coroutine::destroy_by_address(self->_continuation.address()); \
} else { \
[[clang::musttail]] return Coroutine::resume_by_address(self->_continuation.address()); \
}

        static void _static_resume(void* ptr) {
            auto self = (WithDeadlineAwaitable*)ptr;
            CONTINUE
        }

        static void _static_destroy(void* ptr) {
            auto self = (WithDeadlineAwaitable*)ptr;
            set_stopped(self->_outcome);
            CONTINUE
        }

#undef CONTINUE

        std::optional<T> await_resume() {
            _inner_stop_source.request_stop();
            return _outcome.await_resume_stopped_as_optional();
        }

    };

    template<typename T>
    Coroutine::Future<std::optional<T>> with_deadline(std::chrono::steady_clock::time_point deadline, Coroutine::Future<T>&& future) {
        co_return co_await WithDeadlineAwaitable<T>(std::move(deadline), std::move(future));
    };




} // namespace wry

#endif /* kqueue_reactor_hpp */
