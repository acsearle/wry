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


    // ---- with_deadline: a race of the inner against a timer ---------------
    //
    //   optional<T> r = co_await with_deadline(t, sender);
    //
    // A two-entrant race under one interior stop source: entrant 0 is the
    // inner under stopped_as_optional, entrant 1 a timer that yields nullopt
    // at the deadline.  Whichever completes first hastens the other through
    // the interior source; the outer's token is bridged into it.  So the
    // inner's value or error wins if it comes first, and nullopt wins when
    // the deadline passes (the inner is then cancelled) or when the inner
    // stops of its own accord (the timer is then withdrawn).  The race
    // delivers once BOTH entrants have retired.
    //
    // Unlike a plain stopped_as_optional, the outer's own request is not
    // absorbed: a nullopt that is delivered while the outer's token is
    // requested becomes stopped (the bridge cancelled both entrants; the
    // awaiting frame unwinds).  A value that wins is delivered even if the
    // outer is requested by then, as for any sender that completes before
    // it sees the request.

    template<typename U>
    coroutine::Future<std::optional<U>> deadline_timer(std::chrono::steady_clock::time_point deadline) {
        // Measured when the race starts this entrant.  A stop request on the
        // interior token withdraws the sleep and destroys this frame, which
        // is the timer's stopped completion
        co_await cancelable_sleep(std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count());
        co_return std::nullopt;
    }

    template<execution::Sender S>
    struct WithDeadlineSender {
        std::chrono::steady_clock::time_point _deadline;
        S _sender;
    };

} // namespace wry

namespace wry::execution {
    template<Sender S>
    struct SenderTraits<wry::WithDeadlineSender<S>> {
        // optional<monostate> for a void inner, as stopped_as_optional yields
        using value_type = std::optional<typename Outcome<SenderValueType<S>>::stored_type>;
    };
} // namespace wry::execution

namespace wry {

    // The race's receiver: unwraps the winner and propagates the outer's
    // request (see above)
    template<typename V, typename R>
    struct DeadlineReceiver {
        R _receiver;
    };

    template<typename V, typename R>
    void set_value(DeadlineReceiver<V, R>&& receiver, execution::RaceWinner<std::optional<V>>&& winner) {
        // The race delivers on the last retiree's thread.  The bridge is a
        // stop_callback on the outer's token, so a request that reached the
        // entrants happened before their retires, which the race's acq_rel
        // retire count orders before this delivery: if the outer's request
        // is why the winner was nullopt, this load sees it
        if (!winner.value && get_stop_token(receiver._receiver).stop_requested())
            set_stopped(std::move(receiver._receiver));
        else
            set_value(std::move(receiver._receiver), std::move(winner.value));
    }

    template<typename V, typename R>
    void set_error(DeadlineReceiver<V, R>&& receiver, std::exception_ptr&& error) {
        set_error(std::move(receiver._receiver), std::move(error));
    }

    template<typename V, typename R>
    void set_stopped(DeadlineReceiver<V, R>&& receiver) {
        // Only if both entrants stopped; stopped_as_optional never does
        set_stopped(std::move(receiver._receiver));
    }

    template<typename V, typename R>
    std::stop_token get_stop_token(DeadlineReceiver<V, R> const& receiver) {
        return get_stop_token(receiver._receiver);
    }

    template<execution::Sender S, typename R>
    auto connect(WithDeadlineSender<S>&& sender, R&& receiver) {
        using V = typename execution::Outcome<execution::SenderValueType<S>>::stored_type;
        return connect(execution::when_any(execution::stopped_as_optional(std::move(sender._sender)),
                                           deadline_timer<V>(sender._deadline)),
                       DeadlineReceiver<V, std::remove_cvref_t<R>>{std::forward<R>(receiver)});
    }

    template<execution::Sender S>
    WithDeadlineSender<std::remove_cvref_t<S>> with_deadline(std::chrono::steady_clock::time_point deadline, S&& sender) {
        static_assert(!std::is_lvalue_reference_v<S>, "with_deadline consumes a sender: pass an rvalue");
        return WithDeadlineSender<std::remove_cvref_t<S>>{deadline, std::forward<S>(sender)};
    }

    // Anything else a coroutine could co_await (a queue's take, a nursery's
    // join) is lifted into a Future first, awaited in place through a
    // pointer: the argument must outlive the await, which a temporary in
    // the awaiting expression does
    template<typename A>
        requires (!execution::Sender<A>)
    auto with_deadline(std::chrono::steady_clock::time_point deadline, A&& awaitable) {
        return with_deadline(deadline, coroutine::lift_awaitable<A>(&awaitable));
    }


} // namespace wry

#endif /* kqueue_reactor_hpp */
